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
#define CHUNKSIZE  (1<<6)  /* Extend heap by this amount (bytes) */ // CHANGE TO 12

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
static void buckets_init(unsigned int buckets_count, size_t *starting_position);
static void add_to_bucket(size_t *block, size_t *bucket); // largest first or not (this algorithm is linear time)
//static void remove_from_bucket(void *ptr);
static void *find_bucket(size_t words);
static void print_heap();
static size_t *remove_from_bucket(size_t *block_ptr, size_t *bucket);
//static void *find_fit(size_t words);
// static void *find_fit(size_t asize);

// static char *rover;
static char *heap_listp = 0;

// gcc -D MY_MMTEST -Wall -g -m32 mm.c memlib.c -o mymem
#ifdef MY_MMTEST
int main(int argc, char **argv)
{
  mem_init();
  mm_init();

  void *ptr1 = (size_t *) (heap_listp + 2*WSIZE);
  mm_free(ptr1);

  void *ptr2 = extend_heap(CHUNKSIZE/WSIZE);
  mm_free(ptr2);
  //remove_from_bucket(ptr2, find_bucket(CHUNKSIZE/WSIZE));

  void *ptr3 = extend_heap(CHUNKSIZE/WSIZE);
  mm_free(ptr3);

  remove_from_bucket(ptr1, find_bucket(CHUNKSIZE/WSIZE));
  remove_from_bucket(ptr2, find_bucket(CHUNKSIZE/WSIZE));
  remove_from_bucket(ptr3, find_bucket(CHUNKSIZE/WSIZE));
    
  print_heap();

  printf("%p\n", heap_listp + WSIZE);
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
    buckets_init(BUCKETS_COUNT, (size_t *) (heap_listp));
    heap_listp += (BUCKETS_COUNT*WSIZE);  

    PUT(heap_listp, 0);                          /* Alignment padding */
    //printf("Found %d at %p\n", GET(heap_listp), heap_listp);
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */ 
    //printf("Found %d at %p\n", GET(heap_listp + 1 * WSIZE), heap_listp + 1 * WSIZE);  
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */ 
    //printf("Found %d at %p\n", GET(heap_listp + 2 * WSIZE), heap_listp + 2 * WSIZE);  
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     /* Epilogue header */
    //printf("Found %d at %p\n", GET(heap_listp + 3 * WSIZE), heap_listp + 3 * WSIZE);  
    heap_listp += (2*WSIZE);
    //printf("Found %d at heap_listp (%p)\n", GET(heap_listp), heap_listp);  

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) 
        return -1;

    return 0;
}

static void print_heap() {
  size_t *current_word = mem_heap_lo();
  while (current_word <= mem_heap_hi()) {
    printf("             --------------\n");
    printf("%p  |  0x%x\n", current_word, *current_word);
    printf("             --------------\n");
    current_word++;
  }
}

/*
 * buckets_init - Initializes buckets_count buckets at beginning of heap
 * buckets store addresses to head nodes of free linked lists
 * Bucket boundaries are organized as such (ranges inclusive, in bytes):
 *
 * 0x0 : 1        k: 0
 * 0x4 : 2        k: 1
 * 0x8 : 3 - 4    k: 2
 * 0x12: 5 - 8    k: 3
 * 0x16: 9 - 16   k: 4
 * 0x20: 17 - 32  k: 5
 *
 * IMPORTANT NOTE: the bucket ranges are in total free bytes,
 *                 to find a fit for payload size x:
 *                 BUCKET.LOW <= x + 4 <= BUCKET.HIGH
 *                 the 4 represent header, footer, next, and padding of allocated block
 */
static void buckets_init(unsigned int buckets_count, size_t *starting_position) {
    mem_sbrk(WSIZE * BUCKETS_COUNT); // makes room for buckets
    //buckets_count += ALIGNMENT - buckets_count % ALIGNMENT; // aligns buckets to keep alignment of heap
    // printf("Classes required: %d\n", buckets_count);

    // initialize bucket array
    size_t *bucket = starting_position; // initializes bucket array to start of heap
    while (buckets_count != 0) {
      *bucket = 0; // 0 means bucket is empty
      //printf("Assigned %d at %p\n", GET(bucket), bucket);
      bucket++;
      buckets_count--;
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
    //return coalesce(bp);
    return bp;                      
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
 * find_bucket - iterates over buckets array to find smallest bucket
 */
static void *find_bucket(size_t words) {
  // size_t newsize = ALIGN(words + SIZE_T_SIZE);
  // printf("Looking for size %d...\n", newsize);
  int k;
  for (k = 0; k < BUCKETS_COUNT; k++) {
    if (words * WSIZE <= (1 << k)) {
      // printf("k found: %d (%p)\n", k, (int *) mem_heap_lo() + k);
      return (size_t *) mem_heap_lo() + k;
    }
  }
  return 0;
}

/*
 * find_fit - checks first fit inside a given bucket
 *            returns pointer to free block
 *            2 possible algorithms:
 *            I  Linked list is ranked starting largest
 *            II First-fit which could be slow for large requests
 *            *  Splitting optional, would decrease internal fragmentation
 */
static void *find_fit(size_t words) {
  size_t newsize = ALIGN(words + SIZE_T_SIZE);
  size_t *bucket = (size_t *) find_bucket(newsize);
  size_t *node = bucket; // is this right?
  while (bucket < (size_t *) mem_heap_lo() + 32) {

    if (newsize <= GET_SIZE(node)) {
      return node; // no splitting done, splitting needed either here or helper function
    } // since first node is largest (I), move up bucket:
    
    bucket++;
  }
  return 0; // no fit found
}

/*
 * mm_free
 */
void mm_free(void *ptr)
{
  size_t size = GET_SIZE(HDRP(ptr));

  PUT(HDRP(ptr), PACK(size, 0));
  PUT(FTRP(ptr), PACK(size, 0));

  add_to_bucket(ptr, find_bucket(size / WSIZE));
  //coalesce(bp);
}

/*
 * add_to_bucket - helper method for mm_free
 *                 traverses over linked list and places block
 *                 (so that it keep order max to min)
 *
 *  ______________
 * | size | alloc |
 * |    HEADER    |
 * ----------------
 * |    *next     |  -  0x0 if end of list   <-   node  &  <-  block_ptr
 * |              |
 * ----------------
 * |    *prev     |  -  0x0 if start of list
 * |              |
 * ----------------
 * |              |
 * |   PAYLOAD    |
 * |              |
 * |              |
 * ----------------
 * | size | alloc |  
 * |    FOOTER    |
 * ----------------
 */
static void add_to_bucket(size_t *block_ptr, size_t *bucket) {
  size_t *node = (size_t *) *bucket; // node is now address of first free block, if exists; 
  //printf("Address of head node: %d\n", node);
  
  if (node == 0x0) { // bucket empty, set bucket content to block_ptr
    *bucket = block_ptr;
    //*(block_ptr + 1) = bucket;
  } else { // else, bucket has blocks already, place at beginning

    // while ((node + 1) != 0x0) {
    //   node = *(node + 1);
    // } // reached leaf node good for first firt fit algo

    //*(block_ptr + 1) = node;
    *(node + 1) = block_ptr;
    *block_ptr = node;
    *bucket = block_ptr;

  }
  //printf("Bucket content: %p\n", *bucket);
}

/*
 * remove_from_bucket - helper method that removes and returns free block from bucket
 */
static size_t *remove_from_bucket(size_t *block_ptr, size_t *bucket) {
  size_t *node;
  size_t *node2;
  //printf("BUCKET CONTENT: %p\n", *bucket);
  //printf("BLOCK_PTR: %p\n", block_ptr);
  if (*bucket == block_ptr) { // Case 1: start of list // ERROR; this is firing when it shouldnt
    *bucket = *block_ptr;
    if (*bucket != 0x0)
      node = (size_t *) *bucket;
      *(node + 1) = 0x0;

  } else { // Case 2: all other cases
    node = *(block_ptr + 1); // node is prev
    *node = *block_ptr; // asign previous to point to next
    node2 = *node; // asigns node2 to next
    if (node2 != 0x0)
      *(node2 + 1) = node; // connects next to back
  }

}

/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 * Implementation partially taken from CS:APP
 * handles rearangement of blocks
 */
static void *coalesce(void *bp)
{
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); 
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); 
	size_t size = GET_SIZE(HDRP(bp));

	if(prev_alloc && next_alloc) {			/* Case 1 */
		return bp;
	}

	else if (prev_alloc && ! next_alloc) {		/* Case 2 */
		size += GET_SIZE(HDRP(NEXT_BLKP(bp))); 
		PUT(HDRP(bp), PACK(size, 0)); 
		PUT(FTRP(bp), PACK(size, 0));
	}

	else if (!prev_alloc && next_alloc) {		/* Case 3 */
		size += GET_SIZE(HDRP(PREV_BLKP(bp))); 
		PUT(FTRP(bp), PACK(size, 0)); 
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); 
		bp = PREV_BLKP(bp);
	}

	else {						/* Case 4 */ 
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))); 
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); 
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp); 
	} 
	return bp;
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














