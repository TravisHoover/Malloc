//Author: Travis Hoover
//Date: April 18, 2016
//CSCI 3240; Dr. Carroll
//This program defines and implements custom made versions of malloc; specifically the
//functions mm_init, mm_malloc, and mm_realloc and supporting functions

#include <stdio.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"


#define ALIGNMENT 8
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)
#define PACK(size, alloc)   (size | alloc)
#define GET(p)              (*(unsigned int *)(p))
#define PUT(p, val)         (*(unsigned int *)(p) = (val))
#define GET_SIZE(p)         (GET(p) & ~0x7)
#define GET_ALLOC(p)        (GET(p) & 0x1)
#define HDRP(bp)            ((char *)(bp) - WSIZE)
#define FTRP(bp)            ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp)       ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)       ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
#define PUT_HDR_FTR(bp, size, alloc) PUT(HDRP(bp), PACK(size, alloc)); PUT(FTRP(bp), PACK(size, alloc));

static void *heapExtension(size_t);
static void *coalesce(void *);
static void *find_fit(size_t size);
static void *find_fit_from_to(size_t size, void *from, void *to);
static void place(void *bp, size_t size);
static char * heap_listp; //pointer to first block

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *) - 1)
        return -1;

    PUT(heap_listp, 0);                             // Alignment Padding
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));    // Prologue Header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));    // Prologue Footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));        // Epilogue header
    heap_listp += (2 * WSIZE);

    if (heapExtension(CHUNKSIZE / WSIZE) == NULL) {
        return -1;
    }
    return 0;
}



static void *heapExtension(size_t words)
{
    char *bp;
    size_t size;

    // Allocate even number of words to maintain alignment
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;


    // Initialize free block header/footer and the epilogue header
    // PUT_HDR_FTR(bp, size, 0);
    PUT_HDR_FTR(bp, size, 0);
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1));

    return coalesce(bp);
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t adj_size;    // adjusted size for header/footer and alignment
    size_t extend_size; // amount to extend if no fit
    char *bp;

    // ignore pointless calls
    if ( size == 0 ) return NULL;

    if ( size <= DSIZE )
        adj_size = 2 * DSIZE;
    else
        adj_size = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    if ((bp = find_fit(adj_size)) != NULL)
    {
        place(bp, adj_size);
        return bp;
    }

    extend_size = MAX(adj_size, CHUNKSIZE);
    if ((bp = heapExtension(extend_size/WSIZE)) == NULL)
        return NULL;

    place(bp, adj_size);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    size_t size;
    // slight optimization. If it's already freed, skip the coalescing
    if ( GET_ALLOC(HDRP(bp)) == 0 )
        return;

    size = GET_SIZE(HDRP(bp));

    PUT_HDR_FTR(bp, size, 0);
    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *bp, size_t size)
{
    void *new_bp;
    size_t adj_size;
    size_t old_size = GET_SIZE(HDRP(bp));

    // Edge cases
    if (bp == NULL)
        return mm_malloc(size);

    if (size == 0)
    {
        mm_free(bp);
        return NULL;
    }

    // Adjust size to be word aligned and at least big enough for header/footer
    if ( size <= DSIZE )
        adj_size = 2 * DSIZE;
    else
        adj_size = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    // don't do anything if the old size is the same as the new size
    if ( adj_size == old_size )
        return bp;

    // Free current block
    PUT_HDR_FTR(bp, old_size, 0);

    new_bp = coalesce(bp);
    if ( GET_SIZE(HDRP(new_bp)) < adj_size)
    {
        // not enough free space around block, need to find new block
        if ((new_bp = find_fit(adj_size)) == NULL)
        {
            // Still can't find big enough block. Need to expand the heap
            if ((new_bp = heapExtension(MAX(adj_size, CHUNKSIZE)/WSIZE)) == NULL)
                return NULL;
        }
    }

    memmove(new_bp, bp, old_size);
    place(new_bp, adj_size);

    return new_bp;
}

static char *last_find = NULL;
static void *find_fit(size_t size)
{
    if ( last_find == NULL )
    {
        // find fit hasn't run yet. run from beginning to end of heap
        last_find = find_fit_from_to(size, NEXT_BLKP(heap_listp), mem_heap_hi());
        return last_find;
    }

    // find fit from last find to end

    if ( (last_find = find_fit_from_to(size, NEXT_BLKP(last_find), mem_heap_hi())) == NULL )
        // didn't find anything from last find to end. run from beginning to last find
        last_find = find_fit_from_to(size, NEXT_BLKP(heap_listp), last_find);

    return last_find;
}

static void *find_fit_from_to(size_t size, void *from, void *to)
{
    void *bp = from;
    while(!(GET_SIZE(HDRP(bp)) >= size && GET_ALLOC(HDRP(bp)) == 0))
    {
        if ( bp >= to )
            return NULL;

        bp = NEXT_BLKP(bp);
    }

    return bp;
}
 static void place(void *bp, size_t size)
{
    size_t curr_size = GET_SIZE(HDRP(bp)); // current size

    if ((curr_size - size) >= 2*DSIZE)
    {
        PUT_HDR_FTR(bp, size, 1);
        PUT_HDR_FTR(NEXT_BLKP(bp), (curr_size - size), 0);
    }
    else
    {
        PUT_HDR_FTR(bp, curr_size, 1);
    }
}



 static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) //do nothing if both are allocated already
    {
        return bp;
    }
    else if (prev_alloc)
    {
        if ( last_find == NEXT_BLKP(bp) ) //skip block if last find point is pointing to coalesced block
            last_find = NEXT_BLKP(NEXT_BLKP(bp));

        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));

        PUT_HDR_FTR(bp, size, 0);
    }
    else if (next_alloc)
    {
        if ( last_find == bp )
            last_find = NEXT_BLKP(bp);

        size += GET_SIZE(FTRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else
    {
        if ( last_find == NEXT_BLKP(bp) || last_find == bp ) //skip a block if next block is coalesced
            last_find = NEXT_BLKP(NEXT_BLKP(bp));

        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
}
