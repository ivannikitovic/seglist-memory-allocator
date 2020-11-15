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

/* Basic constants and macros */
/* Definitions taken fro CS:APP */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

#define WSIZE       4       /* Word and header/footer size (bytes) */ 
#define DSIZE       8       /* Double word size (bytes) */
#define CHUNKSIZE  (1<<12)  /* Extend heap by this amount (bytes) */

#define BUCKETS_COUNT 32

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) 

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))      
#define PUT(p, val)  (*(unsigned int *)(p) = (val)) 

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)            
#define GET_ALLOC(p) (GET(p) & 0x1)                
#define GET_ALLOC_PREV(p) (GET(p) & 0x2) // 1 if prev allocated, 0 otherwise

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)                  
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void buckets_init(unsigned int buckets_count, int *starting_position);

// static char *rover;
static char *heap_listp = 0;

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
 * Implementation partially taken from CS:APP
 */
int mm_init(void)
{
    // printf("Initializing heap starting at: %p\n", extend_heap(32));
    printf("Heap initialized at: %p\n", mem_heap_lo());
    
    // printf("Heap size: %d bytes\n", mem_heapsize());
    // printf("Checking heap size... %s\n", (mem_heapsize() == 32 * WSIZE) ? "PASS" : "FAIL");

    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) //line:vm:mm:begininit
        return -1;
    buckets_init(BUCKETS_COUNT, (int *) (heap_listp));
    heap_listp += (BUCKETS_COUNT*WSIZE);  

    PUT(heap_listp, 0);                          /* Alignment padding */
    printf("Found %d at %p\n", GET(heap_listp), heap_listp);
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */ 
    printf("Found %d at %p\n", GET(heap_listp + 1 * WSIZE), heap_listp + 1 * WSIZE);  
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */ 
    printf("Found %d at %p\n", GET(heap_listp + 2 * WSIZE), heap_listp + 2 * WSIZE);  
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     /* Epilogue header */
    printf("Found %d at %p\n", GET(heap_listp + 3 * WSIZE), heap_listp + 3 * WSIZE);  
    heap_listp += (2*WSIZE);
    printf("Found %d at heap_listp (%p)\n", GET(heap_listp), heap_listp);  

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) 
        return -1;

    return 0;
}

/*
 * buckets_init - Initializes buckets_count buckets at beginning of heap
 * buckets store addresses to head nodes of free linked lists
 * Bucket boundaries are organized as such (ranges inclusive, in bytes):
 *
 * 0x0 : 1
 * 0x4 : 2 
 * 0x8 : 3 - 4
 * 0x12: 5 - 8
 * 0x16: 9 - 16
 * 0x20: 17 - 32
 *
 * IMPORTANT NOTE: the bucket ranges are in total free bytes,
 *                 to find a fit for payload size x:
 *                 BUCKET.LOW <= x + 2 <= BUCKET.HIGH
 *                 the 2 represent header and footer of allocated block
 */
static void buckets_init(unsigned int buckets_count, int *starting_position) {
    //buckets_count += ALIGNMENT - buckets_count % ALIGNMENT; // aligns buckets to keep alignment of heap
    // printf("Classes required: %d\n", buckets_count);

    // initialize bucket array
    int *bucket = starting_position; // initializes bucket array to start of heap
    while (bucket < starting_position + WSIZE * buckets_count) {
      *bucket = ~0; // -1 means bucket is empty
      printf("Assigned %p at %p\n", GET(bucket), bucket);
      bucket++;
    }
}

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words) 
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; 
    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;                      

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         /* Free block header */  
    PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */  
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */ 

    /* Coalesce if the previous block was free */
    return coalesce(bp);                         
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
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */
static void *coalesce(void *ptr) {
  return ptr;
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














