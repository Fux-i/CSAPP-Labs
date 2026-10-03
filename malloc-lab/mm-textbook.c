/*
 * mm-textbook.c - The allocator described in the textbook (CS:APP ch. 9.9).
 *
 * Implicit free list built out of boundary tags.  Every block looks like
 *
 *         <- 4 -> <---- payload ----> <- 4 ->
 *         +--------+-----------------+--------+
 *         | header |     payload     | footer |
 *         +--------+-----------------+--------+
 *                    ^
 *                    pointer returned by mm_malloc
 *
 * The header and the footer both hold the block size, with the low bit used
 * as the allocated flag.  Blocks are padded to a multiple of 8 bytes with a
 * minimum of 16 bytes, so every payload is 8-byte aligned, and a block that
 * is freed is merged with its neighbours immediately.  mm_malloc scans the
 * list from the prologue (first fit) and the heap grows in 4 KB chunks.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/* word and double word sizes */
#define WSIZE 4
#define DSIZE 8

/* grow the heap by this many bytes at a time */
#define CHUNKSIZE (1 << 12)

/* smallest block we ever hand out, and the smallest remainder worth splitting
 * off (header + footer + one aligned payload word) */
#define MINBLOCK (2 * DSIZE)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* pack a size and an allocated flag into one word */
#define PACK(size, alloc) ((size) | (alloc))

/* read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* read the size and the allocated flag out of the word at address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* given a block pointer bp, compute the addresses of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* given a block pointer bp, compute the block pointers of its neighbours */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

/* points at the prologue block; the first real block sits right after it */
static char *heap_listp = NULL;

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static size_t adjust(size_t size);

/*
 * adjust - round a request up to a whole block size (payload plus the two
 *     boundary tags), never smaller than MINBLOCK.
 */
static size_t adjust(size_t size) {
    if (size <= DSIZE)
        return MINBLOCK;
    return DSIZE * ((size + DSIZE + (DSIZE - 1)) / DSIZE);
}

/*
 * mm_init - initialize the malloc package.
 *
 * Build an empty heap out of an 8-byte prologue block (header and footer,
 * both marked allocated) followed by an epilogue header.  The first word is
 * alignment padding, which is what makes the payload of the first real block
 * land on an 8-byte boundary.
 */
int mm_init(void) {
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);                            /* alignment padding */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); /* prologue header */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); /* prologue footer */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     /* epilogue header */
    heap_listp += (2 * WSIZE);

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}

/*
 * mm_malloc - allocate a block of at least size bytes, 8-byte aligned.
 *     First fit, extending the heap if no free block is big enough.
 */
void *mm_malloc(size_t size) {
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0)
        return NULL;

    asize = adjust(size);

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
 * mm_free - free a block and coalesce it with any free neighbours.
 */
void mm_free(void *ptr) {
    size_t size;

    if (ptr == NULL)
        return;

    size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * mm_realloc - resize a block.
 *     Shrinking keeps the block where it is and hands the tail back to the
 *     free list; growing falls back on mm_malloc plus a copy.
 */
void *mm_realloc(void *ptr, size_t size) {
    void *newptr;
    size_t oldsize;
    size_t asize;
    size_t csize;
    size_t copySize;

    if (ptr == NULL)
        return mm_malloc(size);

    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    oldsize = GET_SIZE(HDRP(ptr)) - DSIZE;
    asize = adjust(size);

    if (asize <= GET_SIZE(HDRP(ptr))) {
        csize = GET_SIZE(HDRP(ptr));
        if (csize - asize >= MINBLOCK) {
            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));
            newptr = NEXT_BLKP(ptr);
            PUT(HDRP(newptr), PACK(csize - asize, 0));
            PUT(FTRP(newptr), PACK(csize - asize, 0));
            coalesce(newptr);
        }
        return ptr;
    }

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;

    copySize = (size < oldsize) ? size : oldsize;
    memcpy(newptr, ptr, copySize);
    mm_free(ptr);
    return newptr;
}

/*
 * extend_heap - extend the heap by the requested number of words and turn
 *     the new space into one free block, coalescing it with a free tail.
 */
static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    /* Keep the heap 8-byte aligned by allocating an even number of words */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((bp = mem_sbrk((int)size)) == (void *)-1)
        return NULL;

    /* Initialise the free block and the new epilogue header */
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    return coalesce(bp);
}

/*
 * coalesce - merge a free block with any free neighbour.
 */
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) { /* nothing to merge */
        return bp;
    } else if (prev_alloc && !next_alloc) { /* merge with the next block */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) { /* merge with the prev block */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else { /* merge with both neighbours */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
}

/*
 * find_fit - first fit search of the implicit free list, starting at the
 *     prologue and stopping at the epilogue (a block of size 0).
 */
static void *find_fit(size_t asize) {
    char *bp;

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))
            return bp;
    }
    return NULL;
}

/*
 * place - put a block of asize bytes at bp, splitting off the remainder if
 *     it is big enough to be a block of its own.
 */
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));

    if ((csize - asize) >= MINBLOCK) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}
