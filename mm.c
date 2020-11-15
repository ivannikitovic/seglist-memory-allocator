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
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* bu username : eg. jappavoo */
    "in",
    /* full name : eg. jonathan appavoo */
    "ivan nikitovic",
    /* email address : jappavoo@bu.edu */
    "in@bu.edu",
    "",
    ""
};

#define MAX_HEAP (20*(1<<20))  /* 20 GB */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

#define WSIZE 4
#define DSIZE 8

#define CHUNKSIZE (1<<12) // 4 kb

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Pack a size and allocated bit into a word to be used as header/footer */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)  // return size
#define GET_ALLOC(p) (GET(p) & 0x1) // 1 if acllocated, 0 otherwise
#define GET_ALLOC_PREV(p) (GET(p) & 0x2) // 1 if prev allocated, 0 otherwise

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE) // useful for buddy
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);

// Pseudo struct for free_block structure
// typedef struct free_block {
//   unsigned int header;
//   struct free_block *next;
//   struct free_block *prev;
//   unsigned int payload[];
// } free_block;

// Pseudo struct for used_block structure
// typedef struct used_block {
//   unsigned int header;
//   unsigned int payload[];
// } used_block;

// gcc -D MY_MMTEST -Wall -g -m32 mm.c memlib.c -o mymem
#ifdef MY_MMTEST
int main(int argc, char **argv)
{
  mem_init();
  mm_init();
  //printf("mm_malloc=%p\n", mm_malloc(4088));
  return 0;
}
#endif

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    printf("Initializing heap starting at: %p\n", extend_heap(32));
    printf("Start: %p\n", mem_heap_lo());
    printf("End: %p\n", mem_heap_hi());
    printf("Heap size: %d bytes\n", mem_heapsize());
    printf("Checking heap size... %s\n", (mem_heapsize() == 32 * WSIZE) ? "PASS" : "FAIL");

    int classes = 1, i = MAX_HEAP;
    while (i >>= 1) { ++classes; };
    printf("Classes required: %d\n", classes);

    // initialize bucket array
    int *bucket = mem_heap_lo(); // initializes bucket array to start of heap
    while ((char *) bucket < (char *) mem_heap_lo() + WSIZE * classes) {
      *bucket = 0;
      printf("Assigned %d at %p\n", *bucket, bucket);
      bucket++;
    }

    return 0;
}

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words) 
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; //line:vm:mm:beginextend
    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;                                        //line:vm:mm:endextend

    /* Initialize free block header/footer and the epilogue header */
    // PUT(HDRP(bp), PACK(size, 0));         /* Free block header */   //line:vm:mm:freeblockhdr
    // PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */   //line:vm:mm:freeblockftr

    /* Coalesce if the previous block was free */
    return bp;                                          //line:vm:mm:returnblock
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    int newsize = ALIGN(size + SIZE_T_SIZE);
    void *p = mem_sbrk(newsize);
    PUT(p + SIZE_T_SIZE, PACK(newsize, 1));
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
void mm_free(void *ptr)
{
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
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














