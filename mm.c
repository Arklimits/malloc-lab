/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include "mm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Team 3",
    /* First member's full name */
    "Arklimits",
    /* First member's email address */
    "https://github.com/Arklimits/swjungle-malloc",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 가용 리스트 조작을 위한 기본 상수 & 매크로 */
#define WSIZE 4              // word size
#define DSIZE 8              // double word size
#define CHUNKSIZE (1 << 12)  // 초기 가용 블록과 힙 확장을 위한 기본 Chunk size (4kb)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))  // size 와 alloc을 합쳐서 block address 제작 (sssss00a)
#define GET_SIZE(p) (GET(p) & ~0x7)           // address에 있는 size 획득 (& 11111000)
#define GET_ALLOC(p) (GET(p) & 0x1)           // address에 있는 alloc 획득 (& 00000001)

#define GET(p) (*(unsigned long *)(p))                              // 인자 p에 들어있는 block address 획득
#define PUT(p, val) (*(unsigned long *)(p) = (unsigned long)(val))  // 인자 p에 다음 block address 할당

#define HDRP(bp) ((char *)(bp)-WSIZE)                                  // header는 block pointer의 Word Size만큼 앞에 위치
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)           // footer는 헤더의 끝 지점부터 block의 사이즈 만큼 더하고 2*word만큼 앞에 위치
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))  // 다음 block pointer 위치로 이동
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))    // 이전 block pointer 위치로 이동

/* for Implicit List */
static char *heap_listp;  // 힙 리스트 포인터

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

/* for Explicit List */
#define PREV_FREE(bp) (*(void **)(bp))          // Predecessor 대신 알아보기 편하게 PREV_FREE로 사용
#define NEXT_FREE(bp) (*(void **)(bp + WSIZE))  // Successor 대신 알아보기 편하게 NEXT_FREE로 사용

static void addfreeblock(void *bp);
static void removefreeblock(void *bp);

/* for Segregated List */
#define SEG_SIZE (12)
#define START(class) (*(void **)((char *)(heap_listp) + (WSIZE * class)))

static int getclass(size_t size);

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
    heap_listp = mem_sbrk((SEG_SIZE + 4) * WSIZE);  // 16 Word + SEG 리스트 만큼 늘리기

    if (heap_listp == (void *)-1)
        return -1;  // 메모리가 꽉찼다면 -1 반환

    PUT(heap_listp, 0);                                                            // Padding 생성
    PUT(heap_listp + (1 * WSIZE), PACK((SEG_SIZE + 2) * WSIZE, 1));                // Prologue header 생성
    for (int i = 0; i < SEG_SIZE; i++) PUT(heap_listp + ((2 + i) * WSIZE), NULL);  // Seg Free List 생성
    PUT(heap_listp + ((SEG_SIZE + 2) * WSIZE), PACK((SEG_SIZE + 2) * WSIZE, 1));   // Prologue Footer 생성
    PUT(heap_listp + ((SEG_SIZE + 3) * WSIZE), PACK(0, 1));                        // Epilogue Header 생성

    heap_listp += DSIZE;

    if (extend_heap(CHUNKSIZE / DSIZE) == NULL)  // extend_heap을 통해 시작할 때 힙을 한번 늘려줌
        return -1;                               // memory가 꽉찼다면 -1 반환

    return 0;
}

/*
 * 가용 블록끼리 연결
 */
void addfreeblock(void *bp) {  // Stack형 구조로 만들었기 때문에
    int class = getclass(GET_SIZE(HDRP(bp)));

    NEXT_FREE(bp) = START(class);  // 현재 블록의 NEXT에 기존의 시작 포인터를 삽입
    PREV_FREE(bp) = NULL;
    PREV_FREE(START(class)) = bp;  // 기존 블록의 PREV를 현재 블록에 연결
    START(class) = bp;             // class의 시작점이 나를 가리키게 함
}

/*
 * 가용 블록 삭제
 */
void removefreeblock(void *bp) {
    int class = getclass(GET_SIZE(HDRP(bp)));

    if (bp == START(class)) {             // 첫번째 블록을 삭제 할 경우
        PREV_FREE(NEXT_FREE(bp)) = NULL;  // 다음 블록의 PREV 초기화
        START(class) = NEXT_FREE(bp);     // free_listp를 다음 블록으로 연결
    } else {
        NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);  // 이전 블록의 NEXT를 다음 블록으로 연결
        PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);  // 다음 블록의 PREV를 이전 블록으로 연결
    }
}

/*
 * 힙 확장
 */
static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    size = words * DSIZE;
    bp = mem_sbrk(size);

    if ((long)bp == -1)
        return NULL;

    /* 메모리 시스템으로부터 추가적인 힙 공간을 요청한다 사용중이지 않으므로 alloc = 0 */
    PUT(HDRP(bp), PACK(size, 0));          // Free Block Header 생성
    PUT(FTRP(bp), PACK(size, 0));          // Free Block Footer 생성
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));  // Epilogue Header 이동

    return coalesce(bp);
}

/*
 * 블록을 연결하는 함수
 */
void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));  // 이전 블록의 가용 여부
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));  // 다음 블록의 가용 여부
    size_t size = GET_SIZE(HDRP(bp));                    // 현재 블록의 크기

    // CASE 1: 이전과 다음 블록이 모두 할당되어 있다면 PASS

    if (prev_alloc && !next_alloc) {            // CASE 2: 이전 블록은 할당상태, 다음블록은 가용상태
        removefreeblock(NEXT_BLKP(bp));         // 다음 블록을 가용 리스트에서 제거
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));  // 현재 블록을 다음 블록까지 포함한 상태로 변경
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    else if (!prev_alloc && next_alloc) {       // CASE 3: 이전 블록은 가용상태, 다음 블록은 할당상태
        removefreeblock(PREV_BLKP(bp));         // 이전 블록을 가용리스트에서 제거
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));  // 현재 블록을 이전 블록까지 포함한 상태로 변경
        PUT(FTRP(bp), PACK(size, 0));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));  // 이전 블록의 헤더 이동
    }

    else if (!prev_alloc && !next_alloc) {  // CASE 4: 이전과 다음 블록 모두 가용상태다.
        removefreeblock(NEXT_BLKP(bp));     // 이전과 다음 블록을 가용리스트에서 제거
        removefreeblock(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));  // 현재 블록을 이전 블록부터 다음 블록까지 포함한 상태로 변경
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
    }

    addfreeblock(bp);  // 가용 리스트에 블록 추가
    return bp;
}

/*
 * 가용한 address를 찾는 함수
 */
void *find_fit(size_t asize) {
    void *bp;

    for (bp = heap_listp; GET_ALLOC(HDRP(bp)) < 1; bp = NEXT_FREE(bp))  // 가용 리스트 포인터에서 출발해서 Eplilogue Header를 만날 때 까지 작동
        if (GET_SIZE(HDRP(bp)) >= asize)                                // 현재 블록이 필요한 size보다 크면 반환
            return bp;
    return NULL;
}

/*
 * 할당 함수
 */
void place(void *bp, size_t asize) {  // 요청한 블록을 가용 블록의 시작 부분에 배치, 나머지 부분의 크기가 최소 블록크기와 같거나 큰 경우에만 분할하는 함수.
    size_t current_size = GET_SIZE(HDRP(bp));
    size_t diff_size = current_size - asize;

    removefreeblock(bp);  // 원래 블록을 가용 리스트에서 제거

    if (diff_size >= (2 * DSIZE)) {
        // printf("block 위치 %p | 들어갈 list의 크기 %d | 넣어야 할 size 크기 %d\n", (int *)bp, GET_SIZE(HDRP(bp)), asize);
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        // printf("free block 위치 %p | 나머지 block 크기 %d\n", (int *)NEXT_BLKP(bp), diff_size);
        PUT(HDRP(bp), PACK(diff_size, 0));
        PUT(FTRP(bp), PACK(diff_size, 0));
        addfreeblock(bp);  // 분할된 블록을 가용 리스트에 추가
        return;
    }
    // printf("block 위치 %p | padding으로 넣은 size 크기 %d\n", (unsigned int *)bp, current_size);
    PUT(HDRP(bp), PACK(current_size, 1));
    PUT(FTRP(bp), PACK(current_size, 1));
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size) {
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0)
        return NULL;

    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    bp = extend_heap(extendsize / DSIZE);
    // printf("사이즈 부족으로 Chuncksize %d 연장\n", extendsize);
    if (bp == NULL)
        return NULL;

    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr) {
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
    if (ptr == NULL)  // pointer가 비어 있으면 malloc 함수와 동일하게 동작
        return mm_malloc(size);

    if (size <= 0) {  // memory size가 0이면 메모리 free
        mm_free(ptr);
        return NULL;
    }

    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    copySize = GET_SIZE(HDRP(ptr));

    if (size < copySize)  // 현재 memory보다 크면 memory를 늘려서 새로 할당
        copySize = size;

    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

int getclass(size_t size) {
    if (size < 16)  // size가 최소 16바이트 보다 작을 시 오류
        return -1;

    size_t class[SEG_SIZE];
    class[0] = 16;

    for (int i = 0; i < SEG_SIZE; i++) {
        if (i > 0)
            class[i] = class[i - 1] << 1;  // 클래스 size 검색
        if (size <= class[i])
            return i;  // 클래스에 해당할 시 클래스 리턴
    }

    return SEG_SIZE - 1;  // size가 8192바이트 초과 시 마지막 클래스로 처리
}