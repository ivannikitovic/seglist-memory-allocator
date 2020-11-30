/*
 * mm.c - Efficient malloc package implemented using seglists.
 * 
 * My solution ended up being a seglist created on the beginning of the heap.
 * The seglist elements hold nodes to unordered linked lists of free blocks.
 * Splitting is done in the function place, and coalescing is done in coalesce.
 * A further challenge would be to implement a buddy system or an ordered list.
 *
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
#define OVERHEAD 32

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
static void *find_bucket(size_t words);
static void *find_fit(size_t words);
static void place(void *bp, size_t size);
static void add_to_bucket(size_t *block, size_t *bucket);
static void add_to_seglist(size_t *ptr);
static void remove_from_bucket(size_t *block_ptr, size_t *bucket);
static void remove_from_seglist(size_t *ptr);
static void print_seglist(void);
static void print_heap(void);

static char *heap_listp = 0;

/*
 * main - used to test mm.c manually
 */
// gcc -D MY_MMTEST -Wall -g -m32 mm.c memlib.c -o mymem
#ifdef MY_MMTEST
int main(int argc, char **argv)
{
  mem_init();
  mm_init();

  void *ptr1 = mm_malloc(40);
  void *ptr2 = mm_malloc(40);
  void *ptr3 = mm_malloc(40);
  mm_free(ptr1);
  mm_free(ptr2);
  mm_free(ptr3);

  mm_check();
  print_heap();
  return 0;
}
#endif

/* 
 * mm_init - initialize the malloc package.
 * Implementation partially taken from CS:APP
 */
int mm_init(void) { 
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) //line:vm:mm:begininit
        return -1;
    buckets_init(BUCKETS_COUNT, (size_t *) (heap_listp));
    heap_listp += (BUCKETS_COUNT*WSIZE);  

    PUT(heap_listp, 0);                          /* Alignment padding */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */ 
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */ 
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     /* Epilogue header */
    heap_listp += (2*WSIZE);

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
 * 0x0 : 1        k: 0
 * 0x4 : 2        k: 1
 * 0x8 : 3 - 4    k: 2
 * 0x12: 5 - 8    k: 3
 * 0x16: 9 - 16   k: 4
 * 0x20: 17 - 32  k: 5
 *
 * IMPORTANT NOTE: the bucket ranges are in total free bytes,
 *                 to find a fit for payload size x:
 *                 BUCKET.LOW <= x + 4*WSIZE <= BUCKET.HIGH
 *                 the 4 represent header, footer, next, and padding of allocated block
 */
static void buckets_init(unsigned int buckets_count, size_t *starting_position) {
    mem_sbrk(WSIZE * BUCKETS_COUNT); // makes room for buckets

    // initialize bucket array
    size_t *bucket = starting_position; // initializes bucket array to start of heap
    while (buckets_count != 0) {
      *bucket = 0; // 0 means bucket is empty
      bucket++; // increment current bucket
      buckets_count--;
    }
}

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 *               implementation partly taken from CS:APP
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
 * mm_malloc - Allocate a block with at least size bytes of payload 
 *             implementation partly taken from CS:APP
 */
void *mm_malloc(size_t size) {
    size_t newsize = ALIGN(size + OVERHEAD); /* Adjusted block size in bytes */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;      

    if (heap_listp == 0) {
        mm_init();
    }

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Search the free list for a fit */
    if ((bp = find_fit(newsize / WSIZE)) != NULL) {
        place(bp, newsize);
        return bp + DSIZE;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(newsize,CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)  
        return NULL;
    place(bp, newsize);
    return bp + DSIZE;
}

/*
 * place - handles splitting and block placement
 */
static void place(void *bp, size_t size)
{
    size_t csize = GET_SIZE(HDRP(bp));  // get size  

    if ((csize - size) >= (4*DSIZE)) { // if possible, split block
        remove_from_seglist(bp); // remove current block
        PUT(HDRP(bp), PACK(size, 1)); // allocate bit
        PUT(FTRP(bp), PACK(size, 1)); // allocate bit
        bp = NEXT_BLKP(bp);

        PUT(HDRP(bp), PACK(csize-size, 0)); // free other chunk
        PUT(FTRP(bp), PACK(csize-size, 0)); // free other chunk
        add_to_seglist(bp); // add to seglist
    }
    else {
        remove_from_seglist(bp); // no splitting, just remove
        PUT(HDRP(bp), PACK(csize, 1)); // allocate bit
        PUT(FTRP(bp), PACK(csize, 1)); // allocate bit
    }
}

/*
 * find_bucket - iterates over buckets array to find smallest bucket
 */
static void *find_bucket(size_t words) {
  int k; // counter value
  for (k = 0; k < BUCKETS_COUNT; k++) { // iterates over seglist
    if (words * WSIZE <= (1 << k)) { // uses shifts to get 2^k and checks size
      return (size_t *) mem_heap_lo() + k; // return address of bucket if possible
    }
  }
  return 0; // otherwise, return 0
}

/*
 * find_fit - checks first fit inside a given bucket
 *            returns pointer to free block
 */
static void *find_fit(size_t words) {
  size_t *node;
  size_t newsize = words;
  size_t *bucket = (size_t *) find_bucket(newsize); // finds bucket for placement
  while (bucket < (size_t *) mem_heap_lo() + BUCKETS_COUNT) { // iterates over seglist if no bucket fits
    node = GET(bucket);

    while (node != 0x0) { // iterates over linked list
      if (newsize * WSIZE <= GET_SIZE(HDRP(node))) // if fit found, return
        return node;
      node = GET(node); // next node
    }
    
    bucket++; // increment bucket
  }
  return 0; // no fit found, return 0
}

/*
 * mm_free - frees a block
 */
void mm_free(void *ptr)
{
  ptr = ptr - DSIZE; // aligns pointer
  size_t size = GET_SIZE(HDRP(ptr)); // gets size of block

  PUT(HDRP(ptr), PACK(size, 0)); // zero out alloc bit
  PUT(FTRP(ptr), PACK(size, 0)); // zero out alloc bit

  coalesce(ptr); // coalesce block if possible
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
 * |              |  -  returned by malloc
 * |   PAYLOAD    |
 * |              |
 * |              |
 * ----------------
 * | size | alloc |  
 * |    FOOTER    |
 * ----------------
 */
static void add_to_bucket(size_t *block_ptr, size_t *bucket) {
  size_t *node =  *bucket; // node is now address of first free block, if exists; 
  PUT(block_ptr, 0x0); // clear next node address
  PUT(block_ptr + 1, 0x0); // clear prev node address

  if (node == 0x0) { // CASE 1: bucket empty, set bucket content to block_ptr
    PUT(bucket, block_ptr);

  } else { // CASE 2: else, bucket has blocks already, place at beginning
    PUT(node + 1, block_ptr); // set prev of node to block
    PUT(block_ptr, node); // set next of block to node
    PUT(bucket, block_ptr); // place block in bucket
  }
}

/*
 * add_to_seglist - container function for
 *                  add_to_bucket
 *
 */
static void add_to_seglist(size_t *ptr) {
  size_t size = GET_SIZE(HDRP(ptr)); // get size of block
  add_to_bucket(ptr, find_bucket(size / WSIZE)); // add block to bucket
}

/*
 * remove_from_bucket - helper method that removes and returns free block from bucket
 *                      modifies the linked list
 */
static void remove_from_bucket(size_t *block_ptr, size_t *bucket) {
  size_t *node;
  size_t *node2;
  if (*bucket == block_ptr) { // CASE 1: start of list
    PUT(bucket, GET(block_ptr)); // set bucket to block
    node = GET(block_ptr); // node is next of block
    if (node != 0x0) { // if not 0, block has a next
        *(node + 1) = 0x0; // set node prev to 0
      }

  } else { // Case 2: all other cases
    node = GET(block_ptr + 1); // node is prev
    if (node != 0x0) { // if not 0, block has a next
      PUT(node, GET(block_ptr)); // assign previous to point to next
    }
    node2 = GET(node); // assigns node2 to next
    if (node2 != 0x0) // if not 0, block has a prev
      PUT((node2 + 1), node); // connects next to back
  }

}

/*
 * remove_from_seglist - container function for
 *                       remove_from_bucket
 *
 */
static void remove_from_seglist(size_t *ptr) {
  size_t size = GET_SIZE(HDRP(ptr)); // get size of block
  remove_from_bucket(ptr, find_bucket(size / WSIZE)); // remove block from bucket
}

/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 *            Implementation partially taken from CS:APP
 *            removes free blocks from lists as it coalesces
 */
static void *coalesce(void *bp) {
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); 
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); 
	size_t size = GET_SIZE(HDRP(bp));

	if(prev_alloc && next_alloc) {			   /* Case 1: both alloced, 
                                                    only add current to seglist */
    add_to_seglist(bp); // add block to seglist
	}

	else if (prev_alloc && ! next_alloc) { /* Case 2: next free, 
                                                    coalesce and add to seglist */
		size += GET_SIZE(HDRP(NEXT_BLKP(bp))); // increase size by next

    remove_from_seglist( NEXT_BLKP(bp) ); // remove next block from seglist

		PUT(HDRP(bp), PACK(size, 0)); // zero out alloc bit
		PUT(FTRP(bp), PACK(size, 0)); // zero out alloc bit

    add_to_seglist(bp); // add coalesced block to seglist
	}

	else if (!prev_alloc && next_alloc) {	 /* Case 3: prev free,
                                                    coalesce and add to seglist */
		size += GET_SIZE(HDRP(PREV_BLKP(bp))); // increase size by previous

    remove_from_seglist( PREV_BLKP(bp) ); // remove prev block from seglist

		PUT(FTRP(bp), PACK(size, 0)); // zero out alloc bit
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // zero out alloc bit
		bp = PREV_BLKP(bp); // change block pointer to previous

    add_to_seglist(bp); // add coalesced block to seglist
	}

	else {						                     /* Case 4: both free,
                                                    coalesce and add to seglist */ 
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))); 
    // increase size by previous and next
    remove_from_seglist( NEXT_BLKP(bp) ); // remove next block from seglist
    remove_from_seglist( PREV_BLKP(bp) ); // remove prev block from seglist

		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // zero out alloc bit
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0)); // zero out alloc bit
		bp = PREV_BLKP(bp); // change block pointer to previous

    add_to_seglist(bp); // add coalesced block to seglist
	} 
	return bp; // return block pointer
}

/*
 * mm_realloc - Simple implementation of realloc
 *              using mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
    size_t csize; // current block size
    void *newptr; // new block

    // if ptr is NULL, the call is equivalent to mm_malloc(size)
    if(ptr == NULL) {
        return mm_malloc(size);
    }

    // if size is equal to zero, the call is equivalent to mm free(ptr)
    if(size == 0) {
        mm_free(ptr);
        return 0;
    }

    newptr = mm_malloc(size); // create new block with size size

    csize = GET_SIZE(HDRP(ptr - DSIZE)); // locates header and finds size
    if(size < csize) // if requested size is less than current size take max
      csize = size;
    memcpy(newptr, ptr, csize); // copy the data from old to new block

    mm_free(ptr); // free the old block

    return newptr;
}

/*
 * print_seglist - prints the current seglist
 *                 by looping over seglist
 */
static void print_seglist(void) {
  size_t *current_word = mem_heap_lo(); // initializes current_word at heap start
  while (current_word <= (size_t *) mem_heap_lo() + BUCKETS_COUNT) { // loops over seglist
    printf("             --------------\n"); // upper border
    printf("%p  |  0x%x\n", current_word, *current_word); // prints address and content
    printf("             --------------\n"); // lower border
    current_word++; // increments address
  }
}

/*
 * print_heap - prints entire heap
 */
static void print_heap(void) {
  size_t *current_word = mem_heap_lo();
  while (current_word <= mem_heap_hi()) {
    printf("             --------------\n"); // upper border
    printf("%p  |  0x%x\n", current_word, *current_word);
    printf("             --------------\n"); // lower border
    current_word++;
  }
}

/*
 * mm_check - heap consistency checker
 */
int mm_check(void) {
  size_t *bp;

  /* 1. Checks whether headers and footer match */
  bp = NEXT_BLKP(heap_listp);
  while (bp <= mem_heap_hi()) {
    if (GET(HDRP(bp)) != GET(FTRP(bp))) {
      printf("ERROR: header and footer do not match!\n");
      return 0;
    }
    bp = NEXT_BLKP(bp);
  }

  /* 2. Checks whether allocated blocks are in seglist */
  size_t *node;
  size_t *bucket = (size_t *) mem_heap_lo();
  while (bucket < (size_t *) mem_heap_lo() + BUCKETS_COUNT) { // iterates over seglist
    node = GET(bucket);
    while (node != 0x0) { // iterates over linked list
      if (GET_ALLOC(HDRP(node)) != 0) // if block isn't free, return error
        printf("ERROR: allocated block in free seglist!\n");
        return 0;
      node = GET(node); // next node
    }
    bucket++; // increment bucket
  }

  /* 3. Checks if blocks escaped coalescing */
  bp = NEXT_BLKP(heap_listp);
  while (bp <= mem_heap_hi()) {
    if (GET_ALLOC(HDRP(bp)) == 0 && GET_ALLOC(HDRP(NEXT_BLKP(bp))) == 0) {
      printf("ERROR: blocks not coalesced!\n");
      return 0;
    }
    bp = NEXT_BLKP(bp);
  }

  return 1; // heap is consistent
}