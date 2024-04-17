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

#define PACK(size, alloc) ((size) | (alloc))  // size 와 alloc을 합쳐서 block address 제작 (sssssaaa)
#define GET_SIZE(p) (GET(p) & ~0x7)           // address에 있는 size 획득 (& 11111000)
#define GET_ALLOC(p) (GET(p) & 0x1)           // address에 있는 alloc 획득 (& 00000111)

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
#define SEG_SIZE (20)
#define START(class) (*(void **)((char *)(heap_listp) + (WSIZE * class)))

static int getclass(size_t size);

/* for Optimize Realloc */
static void *replace(void *bp, size_t size, size_t current_size);
static void *replace_slice(void *bp, size_t size, size_t current_size);

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

    // if (extend_heap(CHUNKSIZE / DSIZE) == NULL)  // extend_heap을 통해 시작할 때 힙을 한번 Chunksize만큼 확장
    //     return -1;                               // 최적화를 위해 allocate할 때만 힙을 확장하도록 변경

    return 0;
}

/*
 * addfreeblock - 가용 블록끼리 연결
 */
void addfreeblock(void *bp) {  // Stack형 구조로 만들었기 때문에
    int class = getclass(GET_SIZE(HDRP(bp)));

    NEXT_FREE(bp) = START(class);  // 현재 블록의 NEXT에 기존의 시작 포인터를 삽입
    PREV_FREE(bp) = NULL;
    if (START(class) != NULL)
        PREV_FREE(START(class)) = bp;  // 기존 블록의 PREV를 현재 블록에 연결
    START(class) = bp;                 // class의 시작점이 나를 가리키게 함
}

/*
 * removefreeblock - 가용 블록 삭제
 */
void removefreeblock(void *bp) {
    int class = getclass(GET_SIZE(HDRP(bp)));

    if (bp == START(class))            // 첫번째 블록을 삭제 할 경우
        START(class) = NEXT_FREE(bp);  // class의 시작점을 다음 블록으로 연결
    else {
        NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);      // 이전 블록의 NEXT를 다음 블록으로 연결
        if (NEXT_FREE(bp) != NULL)                     // 다음 블록이 있을 경우에
            PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);  // 다음 블록의 PREV를 이전 블록으로 연결
    }
}

/*
 * extend_heap - 힙 확장
 */
static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    size = words * WSIZE;
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
 * coalesce - 블록을 연결하는 함수
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
void *find_fit(size_t asize) {  // Best-Fit 적용
    int class;
    void *bp;
    void *best = NULL;

    for (class = getclass(asize); class < SEG_SIZE; class ++) {  // 클래스 내에서 찾다가 넣을 수 있는 가용 블록이 없으면 다음 클래스에서 탐색
        for (bp = START(class); bp != NULL; bp = NEXT_FREE(bp))
            if (GET_SIZE(HDRP(bp)) >= asize) {
                if (GET_SIZE(HDRP(bp)) == asize)  // 딱 맞는 사이즈가 있을 경우 더이상 탐색하지 않고 bp 반환
                    return bp;

                best = bp;  // best에 일단 들어갈 수 있는 bp 넣고 다시 탐색 (if문 수행을 줄이기 위해 for문 분할)
                break;
            }

        for (; bp != NULL; bp = NEXT_FREE(bp))
            if (GET_SIZE(HDRP(bp)) >= asize) {
                if (GET_SIZE(HDRP(bp)) == asize)
                    return bp;

                if ((long)(GET_SIZE(HDRP(bp)) < (long)(GET_SIZE(HDRP(best)))))
                    best = bp;
            }
        if (best != NULL)  // class에서 best를 이미 할당했을 경우 다른 class에서 탐색하지 않고 탈출
            break;
    }

    return best;
}

/*
 * place - 할당 및 분할 함수
 */
void place(void *bp, size_t asize) {
    size_t current_size = GET_SIZE(HDRP(bp));
    size_t diff_size = current_size - asize;

    removefreeblock(bp);  // 원래 블록을 가용 리스트에서 제거

    if (diff_size >= (2 * DSIZE)) {     // 사용하고 남은 용량이 최소 사이즈 (16)보다 클 때
        PUT(HDRP(bp), PACK(asize, 1));  // 필요한 용량만큼 사용
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);

        PUT(HDRP(bp), PACK(diff_size, 0));  // 나머지 용량을 다시 가용 블록으로 할당
        PUT(FTRP(bp), PACK(diff_size, 0));
        addfreeblock(bp);  // 분할된 블록을 가용 리스트에 추가
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
    // size_t extendsize;
    char *bp;

    if (size == 0)
        return NULL;

    if (size <= DSIZE)  // 최소 size만큼 만들거나
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);  // 8의 배수로 올림 처리

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    // extendsize = MAX(asize, CHUNKSIZE);
    bp = extend_heap(asize / WSIZE);  // 최적화를 위해 chunksize가 아닌 필요한 만큼 확장
    if (bp == NULL)
        return NULL;

    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    coalesce(bp);
}

/*
 * replace - 재할당을 위한 place 함수 - 주변 노드가 가용 시 통째로 사용
 */
void *replace(void *bp, size_t size, size_t current_size) {
    PUT(HDRP(bp), PACK(current_size, 1));
    PUT(FTRP(bp), PACK(current_size, 1));

    return bp;
}

/**
 * replace_slice - 재할당을 위한 place 함수 - 사용 하고 남는 용량이 최소사이즈 보다 클 때 분할해서 할당하는 함수
 *                 이 함수를 사용하면 메모리를 더 효율적으로 사용하는 것 같은 데 Test Score가 내려가서 사용하지 않았다.
 */
void *replace_slice(void *bp, size_t size, size_t current_size) {
    size_t diff_size = current_size - size;

    if (diff_size >= (2 * DSIZE)) {    // 사용하고 남은 용량이 최소 사이즈 (16)보다 클 때
        PUT(HDRP(bp), PACK(size, 1));  // 필요한 용량만큼 사용
        PUT(FTRP(bp), PACK(size, 1));
        PUT(HDRP(NEXT_BLKP(bp)), PACK(diff_size, 0));  // 나머지 용량을 다시 가용 블록으로 할당
        PUT(FTRP(NEXT_BLKP(bp)), PACK(diff_size, 0));

        addfreeblock(NEXT_BLKP(bp));  // 분할된 블록을 가용 리스트에 추가
        return bp;
    }

    PUT(HDRP(bp), PACK(current_size, 1));
    PUT(FTRP(bp), PACK(current_size, 1));

    return bp;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *bp, size_t size) {
    size_t asize;

    if (bp == NULL)  // pointer가 비어 있으면 malloc 함수와 동일하게 동작
        return mm_malloc(size);

    if (size <= 0) {  // memory size가 0이면 메모리 free
        mm_free(bp);
        return NULL;
    }

    if (size <= DSIZE)  // malloc 할 때 처럼 블록의 size를 정형화
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    size_t copySize = GET_SIZE(HDRP(bp)) - DSIZE;  // only Payload

    if (asize <= copySize)  // 재할당 사이즈가 기존 size보다 작으면 무시
        return bp;

    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));  // 다음 블록의 가용상태
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));  // 이전 블록의 가용상태

    size_t curr_next_size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(NEXT_BLKP(bp)));                                       // 현재 + 다음 블록 size
    size_t curr_prev_size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(PREV_BLKP(bp)));                                       // 현재 + 이전 블록 size
    size_t curr_prev_next_size = GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(NEXT_BLKP(bp)));  // 이전 + 현재 + 다음 블록 size

    if (!prev_alloc && !next_alloc && curr_prev_next_size >= asize) {  // 이전 블록 및 다음 블록이 할당 중이 아니고 전체 용량이 realloc되어야 하는 size보다 클 때
        removefreeblock(NEXT_BLKP(bp));                                // 다음 블록을 가용 리스트에서 제거
        removefreeblock(PREV_BLKP(bp));                                // 이전 블록을 가용 리스트에서 제거
        bp = PREV_BLKP(bp);                                            // 이전 블록으로 이동
        memmove(bp, NEXT_BLKP(bp), asize);                             // 기존 메모리를 현재 블록으로 이동시키고

        return replace(bp, asize, curr_prev_next_size);
    } /*** Info : 자신을 제외하고 앞뒤 둘다 가용인 경우는 test case에는 존재하지 않는다 (어떤 코드를 넣어도 영향이 없음을 확인) ***/

    if (!next_alloc && curr_next_size >= asize) {  // 다음 블록이 할당 중이 아니고 다음 블록 + 현재 블록의 용량이 realloc되어야 하는 size보다 클 때
        removefreeblock(NEXT_BLKP(bp));            // 다음 블록을 가용 리스트에서 제거

        return replace(bp, asize, curr_next_size); 
    }

    if (!prev_alloc && curr_prev_size >= asize) {  // 이전 블록이 할당 중이 아니고 이전 블록 + 현재 블록의 용량이 realloc되어야 하는 size보다 클 때
        removefreeblock(PREV_BLKP(bp));            // 이전 블록을 가용 리스트에서 제거하고
        bp = PREV_BLKP(bp);                        // 이전 블록으로 이동
        memmove(bp, NEXT_BLKP(bp), asize);         // 기존 메모리를 현재 블록으로 이동

        return replace(bp, asize, curr_prev_size);
    }

    void *newptr = mm_malloc(size); // 해당 사항이 없을 경우 새로운 블록을 할당

    if (newptr == NULL) // 할당에 실패했을 경우 반환
        return NULL;

    memcpy(newptr, bp, copySize);
    mm_free(bp); //기존 블록 해제

    return newptr;
}

int getclass(size_t size) {
    size_t class_size = 16;

    for (int i = 0; i < SEG_SIZE; i++) {
        if (size <= class_size)
            return i;      // 클래스에 해당할 시 클래스 리턴
        class_size <<= 1;  // class size 증가
    }

    return SEG_SIZE - 1;  // size가 65536바이트 초과 시 마지막 클래스로 처리
}