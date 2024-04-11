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
    "Seong Ho Lee",
    /* Second member's email address (leave blank if none) */
    "lsh6451217@gmail.com"};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 가용 리스트 조작을 위한 기본 상수 & 매크로 */
#define WSIZE 4              // word size
#define DSIZE 8              // double word size
#define CHUNKSIZE (1 << 12)  // 초기 가용 블록과 힙 확장을 위한 기본 Chunk size (4kb)

#define MAX(x, y) (x > y ? x : y)

#define PACK(size, alloc) (size | alloc)  // size 와 alloc을 합쳐서 주소를 만드는 매크로 (sssss00a)
#define GET_SIZE(p) (GET(p) & ~0x7)       // 주소 p에 있는 size get (& 11111000)
#define GET_ALLOC(p) (GET(p) & 0x1)       // 주소 p에 있는 alloc get (& 00000001)

#define GET(p) (*(unsigned int *)(p))                             // 인자 p가 가리키는 block get
#define PUT(p, val) (*(unsigned int *)(p) = (unsigned int)(val))  // 인자 p에 다음 block의 주소 put

#define HDRP(bp) ((char *)(bp)-WSIZE)                                  // Header는 block pointer의 Word Size만큼 앞에 위치
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)           // Footer는 헤더의 끝 지점부터 block의 사이즈 만큼 더하고 2*word만큼 앞에 위치
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))  // 다음 block pointer 위치로 이동
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))    // 이전 block pointer 위치로 이동

static char *heap_listp;  // 처음에 사용할 가용블록 힙

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)  // old brk에서 4*Word Size만큼 늘려서 mem brk로 늘림
        return -1;
    PUT(heap_listp, 0);                             // Padding 생성
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));  // Prologue header 생성
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));  // Prologue Footer 생성
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));      // Epilogue Header 생성
    heap_listp += (2 * WSIZE);                      // 포인터를 Prologue Header 뒤로 이동

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)  // extend_heap을 통해 시작할 때 힙을 한번 늘려줌
        return -1;

    return 0;
}

/*

*/
static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    /* 요청한 word의 크기를 인접 2워드의 배수(8바이트)로 반올림한다 */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    /* 메모리 시스템으로부터 추가적인 힙 공간을 요청한다 사용중이지 않으므로 alloc = 0 */
    PUT(HDRP(bp), PACK(size, 0));          // Free Block Header 생성
    PUT(FTRP(bp), PACK(size, 0));          // Free Block Footer 생성
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));  // Epilogue Header 이동

    return coalesce(bp);
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size) {
    int newsize = ALIGN(size + SIZE_T_SIZE);
    void *p = mem_sbrk(newsize);
    if (p == (void *)-1)
        return NULL;
    else {
        *(size_t *)p = size;
        return (void *)((char *)p + SIZE_T_SIZE);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr) {
    free(ptr);
    ptr = NULL;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}
