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
    "Jae Hyeok Jeung",
    /* First member's email address */
    "zaqokm2@gmail.com",
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

#define GET(p) (*(unsigned int *)(p))                             // 인자 p에 들어있는 block address 획득
#define PUT(p, val) (*(unsigned int *)(p) = (unsigned int)(val))  // 인자 p에 다음 block address 할당

#define HDRP(bp) ((char *)(bp)-WSIZE)                                  // header는 block pointer의 Word Size만큼 앞에 위치
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)           // footer는 헤더의 끝 지점부터 block의 사이즈 만큼 더하고 2*word만큼 앞에 위치
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))  // 다음 block pointer 위치로 이동
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))    // 이전 block pointer 위치로 이동

/* 명시적 가용 리스트(Explicit Free List) Flag */
#define EXPLICIT

#ifdef EXPLICIT
#define PRED(bp) (*(void **)(bp))          // predecessor
#define SUCC(bp) (*(void **)(bp + WSIZE))  // successor

static char *free_listp;                   // 가용리스트의 첫번째 블록 포인터
static void addfreeblock(void *bp);
static void removefreeblock(void *bp);
#endif

static char *heap_listp;  // 처음에 사용할 가용블록 힙 리스트 포인터

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == -1)  // old brk에서 4*Word Size만큼 늘려서 mem brk로 늘림
        return -1;                                 // 메모리가 꽉찼다면 -1 반환
#ifdef EXPLICIT

    PUT(heap_listp, 0);                          // Padding 생성
    PUT(heap_listp + (1 * WSIZE), PACK(16, 1));  // Prologue header 생성
    PUT(heap_listp + (2 * WSIZE), NULL);         // Prologue PREC Pointer NULL로 초기화
    PUT(heap_listp + (3 * WSIZE), NULL);         // Prologue SUCC Pointer NULL로 초기화
    PUT(heap_listp + (4 * WSIZE), PACK(16, 1));  // Prologue Footer 생성
    PUT(heap_listp + (5 * WSIZE), PACK(0, 1));   // Epilogue Header 생성

    free_listp = heap_listp + DSIZE;

#else

    PUT(heap_listp, 0);                             // Padding 생성
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));  // Prologue header 생성
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));  // Prologue Footer 생성
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));      // Epilogue Header 생성

    heap_listp += DSIZE;  // 포인터를 Prologue Header 뒤로 이동

#endif

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)  // extend_heap을 통해 시작할 때 힙을 한번 늘려줌
        return -1;                               // memory가 꽉찼다면 -1 반환

    return 0;
}

/*
 *   새로운 힙 확장
 */
static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    /* 요청한 word의 크기를 인접 2워드의 배수(8바이트)로 반올림한다 */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((int)(bp = mem_sbrk(size)) == -1)
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

    if (prev_alloc && !next_alloc) {  // CASE 2: 이전 블록은 할당상태, 다음블록은 가용상태

#ifdef EXPLICIT
        removefreeblock(NEXT_BLKP(bp));  // 다음 블록을 가용 리스트에서 제거
#endif

        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));  // 현재 블록을 다음 블록까지 포함한 상태로 변경
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    else if (!prev_alloc && next_alloc) {  // CASE 3: 이전 블록은 가용상태, 다음 블록은 할당상태

#ifdef EXPLICIT
        removefreeblock(PREV_BLKP(bp));  // 이전 블록을 가용리스트에서 제거
#endif

        size += GET_SIZE(HDRP(PREV_BLKP(bp)));  // 현재 블록을 이전 블록까지 포함한 상태로 변경
        PUT(FTRP(bp), PACK(size, 0));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));  // 이전 블록의 헤더 이동
    }

    else {  // CASE 4: 이전과 다음 블록 모두 가용상태다.

#ifdef EXPLICIT
        removefreeblock(NEXT_BLKP(bp));  // 이전과 다음 블록을 가용리스트에서 제거
        removefreeblock(PREV_BLKP(bp));
#endif

        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));  // 현재 블록을 이전 블록부터 다음 블록까지 포함한 상태로 변경
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
    }

#ifdef EXPLICIT
    addfreeblock(bp);  // 가용 리스트에 블록 추가
#endif
    return bp;
}

/*
 * 가용한 address를 찾는 함수
 */
void *find_fit(size_t asize) {
    void *bp;

#ifdef EXPLICIT
    for (bp = free_listp; GET_ALLOC(HDRP(bp)) > 0; bp = SUCC(bp))  // 가용 리스트 포인터에서 출발해서 Eplilogue Header를 만날 때 까지 작동
        if (GET_SIZE(HDRP(bp)) >= asize)                           // 현재 블록이 필요한 size보다 크면 반환
            return bp;
#else
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))  // 힙 포인터(root 역할)에서 출발해서 Epilogue Header를 만날 때 까지 작동
        if (!GET_ALLOC(HDRP(bp)) && (GET_SIZE(HDRP(bp) >= asize)))     // 현재 블록이 가용하고 필요한 size보다 크면 반환
            return bp;
#endif

    return NULL;
}

/*
 * 할당 함수
 */
void place(void *bp, size_t asize) {  // 요청한 블록을 가용 블록의 시작 부분에 배치, 나머지 부분의 크기가 최소 블록크기와 같거나 큰 경우에만 분할하는 함수.
    size_t current_size = GET_SIZE(HDRP(bp));
    size_t diff_size = current_size - asize;

    if (diff_size >= (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(diff_size, 0));
        PUT(FTRP(bp), PACK(diff_size, 0));
        return;
    }

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
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
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
