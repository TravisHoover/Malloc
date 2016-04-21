//Author: Travis Hoover
//Date: April 20, 2016
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
#define PACK(size, alloc)           (size | alloc)
#define GET(p)                      (*(unsigned int *)(p))
#define PUT(p, val)                 (*(unsigned int *)(p) = (val))
#define GET_SIZE(p)                 (GET(p) & ~0x7)
#define GET_ALLOC(p)                (GET(p) & 0x1)
#define HDRP(blockPtr)              ((char *)(blockPtr) - WSIZE)
#define FTRP(blockPtr)              ((char *)(blockPtr) + GET_SIZE(HDRP(blockPtr)) - DSIZE)
#define NEXT_BLKP(blockPtr)         ((char *)(blockPtr) + GET_SIZE(((char *)(blockPtr) - WSIZE)))
#define PREV_BLKP(blockPtr)         ((char *)(blockPtr) - GET_SIZE(((char *)(blockPtr) - DSIZE)))

static char * heap_listp = 0; //pointer to first block
static void *heapExtension(size_t);
static void place(void *blockPtr, size_t size);
static void *find_fit(size_t size);
static void *coalesce(void *);
static void *first_fit(size_t size, void *from, void *to);
static char *lastFind = NULL;

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /*Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *) - 1)
        return -1;

    PUT(heap_listp, 0);                             // Alignment Padding
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));    // Prologue Header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));    // Prologue Footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));        // Epilogue header
    heap_listp += (2 * WSIZE);

    return 0;
}


static void *heapExtension(size_t x)
{
    char *blockPtr;
    size_t size;

    // Allocate even number of x to maintain alignment
    if (x % 2)
        size = (x + 1) * WSIZE;
    else
        size = x * WSIZE;

    if ((long)(blockPtr = mem_sbrk(size)) == -1)
        return NULL;


    //initialize data block
    PUT(HDRP(blockPtr), PACK(size, 0));
    PUT(FTRP(blockPtr), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(blockPtr)), PACK(0,1));

    return coalesce(blockPtr);
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t newSize;    // adjusted block
    size_t extendSize; // amount to extend if no fit
    char *blockPtr;


    if ( size <= DSIZE )
        newSize = 2 * DSIZE;
    else
        newSize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    if ((blockPtr = find_fit(newSize)) != NULL)
    {
        place(blockPtr, newSize);
        return blockPtr;
    }

    //if no fit found, get more memory and place block
    extendSize = MAX(newSize, CHUNKSIZE);
    if ((blockPtr = heapExtension(extendSize/WSIZE)) == NULL)
        return NULL;

    place(blockPtr, newSize);
    return blockPtr;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *blockPtr)
{
    if (blockPtr == 0 )
        return;

    size_t size = GET_SIZE(HDRP(blockPtr));

    PUT(HDRP(blockPtr), PACK(size, 0));
    PUT(FTRP(blockPtr), PACK(size, 0));
    coalesce(blockPtr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    size_t oldSize = GET_SIZE(HDRP(ptr));
    void *newPtr;
    size_t newSize;

    //if ptr is null then this is malloc
    if (ptr == NULL)
        return mm_malloc(size);

    //if the size is 0, block is free; return null
    if (size == 0)
    {
        mm_free(ptr);
        return 0;
    }

    //Compensate to have space for header and footer
    if ( size <= DSIZE )
        newSize = 2 * DSIZE;
    else
        newSize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    //ignore if same size
    if ( newSize == oldSize )
        return ptr;

    // Free block
    PUT(HDRP(ptr), PACK(oldSize, 0));
    PUT(FTRP(ptr), PACK(oldSize, 0));

    newPtr = coalesce(ptr);
    if ( GET_SIZE(HDRP(newPtr)) < newSize)
    {
        if ((newPtr = find_fit(newSize)) == NULL)
        {
            //expand heap
            if ((newPtr = heapExtension(MAX(newSize, CHUNKSIZE)/WSIZE)) == NULL)
                return NULL;
        }
    }

    memmove(newPtr, ptr, oldSize);
    place(newPtr, newSize);

    return newPtr;
}


static void *coalesce(void *blockPtr)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(blockPtr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(blockPtr)));
    size_t size = GET_SIZE(HDRP(blockPtr));

    if (prev_alloc && next_alloc) //do nothing if both are allocated already
    {
        return blockPtr;
    }
    else if (prev_alloc)
    {
        if ( lastFind == NEXT_BLKP(blockPtr) ) //skip block if last find point is pointing to coalesced block
            lastFind = NEXT_BLKP(NEXT_BLKP(blockPtr));

        size += GET_SIZE(HDRP(NEXT_BLKP(blockPtr)));

        PUT(HDRP(blockPtr), PACK(size, 0));
        PUT(FTRP(blockPtr), PACK(size, 0));
    }
    else if (next_alloc)
    {
        if ( lastFind == blockPtr )
            lastFind = NEXT_BLKP(blockPtr);

        size += GET_SIZE(FTRP(PREV_BLKP(blockPtr)));
        PUT(FTRP(blockPtr), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(blockPtr)), PACK(size, 0));
        blockPtr = PREV_BLKP(blockPtr);
    }
    else
    {
        if ( lastFind == NEXT_BLKP(blockPtr) || lastFind == blockPtr ) //skip a block if next block is coalesced
            lastFind = NEXT_BLKP(NEXT_BLKP(blockPtr));

        size += GET_SIZE(HDRP(PREV_BLKP(blockPtr))) + GET_SIZE(FTRP(NEXT_BLKP(blockPtr)));
        PUT(HDRP(PREV_BLKP(blockPtr)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(blockPtr)), PACK(size, 0));
        blockPtr = PREV_BLKP(blockPtr);
    }
    return blockPtr;
}

static void *find_fit(size_t size)
{
    if ( lastFind == NULL ) //if lastFind is null, find_fit has not ran yet
    {
        lastFind = first_fit(size, NEXT_BLKP(heap_listp), mem_heap_hi());
        return lastFind;
    }

    if ( (lastFind = first_fit(size, NEXT_BLKP(lastFind), mem_heap_hi())) == NULL )
        lastFind = first_fit(size, NEXT_BLKP(heap_listp), lastFind);

    return lastFind;
}

static void *first_fit(size_t size, void *x, void *y)
{
    void *blockPtr = x;
    while(!(GET_SIZE(HDRP(blockPtr)) >= size && GET_ALLOC(HDRP(blockPtr)) == 0))
    {
        if (blockPtr > y)
            return NULL;

        blockPtr = NEXT_BLKP(blockPtr);
    }

    return blockPtr;
}
 static void place(void *blockPtr, size_t size)
{
    size_t currentSize = GET_SIZE(HDRP(blockPtr));

    if ((currentSize - size) >= (2 * DSIZE))
    {
        PUT(HDRP(blockPtr), PACK(size, 1));
        PUT(FTRP(blockPtr), PACK(size, 1));
        PUT(HDRP(NEXT_BLKP(blockPtr)), PACK(currentSize - size, 0));
        PUT(FTRP(NEXT_BLKP(blockPtr)), PACK(currentSize - size, 0));
    }
    else
    {
        PUT(HDRP(blockPtr), PACK(currentSize, 1));
        PUT(FTRP(blockPtr), PACK(currentSize, 1));
    }
}
