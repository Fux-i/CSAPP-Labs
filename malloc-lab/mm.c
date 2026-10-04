/*
 * mm.c - A segregated-fit allocator with boundary tags.
 *
 * Blocks keep the textbook 4-byte header/footer framing (payloads stay
 * 8-byte aligned, free neighbours merge immediately), but four changes make
 * it substantially faster and denser than the implicit-list reference:
 *
 *   1. Free blocks live on ~70 segregated doubly-linked free lists keyed by
 *      block size, so mm_malloc never walks the heap.  It picks the best fit
 *      within the first size class that can hold the request.
 *
 *   2. A free block is carved from one of its two ends depending on the
 *      request: small blocks come off the low end (leaving the remainder
 *      above), large blocks off the high end.  Small and large allocations
 *      therefore migrate towards opposite ends of a region instead of
 *      interleaving, which keeps freed same-size runs contiguous.  That is
 *      what lets the binary traces reuse their holes instead of growing the
 *      heap again.
 *
 *   3. mm_realloc resizes in place whenever it can: shrinking hands the tail
 *      back, growing swallows a following free block, and a block whose free
 *      tail ends the heap takes only the missing brk bytes rather than its
 *      whole size again.  Failing that it slides down into a free
 *      predecessor.  Copying to a new block is the last resort.
 *
 *   4. The heap grows in small increments (CHUNKSIZE), so the high-water
 *      mark stays close to the live payload.
 *
 * Block layout (bp is the payload pointer; sizes are multiples of 8):
 *
 *     allocated:  [ size|1 ][ payload .................. ][ size|1 ]
 *     free:       [ size|0 ][ next ][ prev ][ .......... ][ size|0 ]
 *                  ^bp-4    ^bp     ^bp+8                  ^bp+size-8
 *
 * The heap opens with a 4-byte pad and an 8-byte allocated prologue block
 * and closes with a zero-size allocated epilogue header, so coalescing never
 * has to special-case the ends of the heap.
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

/* bytes of fresh heap requested when nothing on the free lists fits */
#define CHUNKSIZE (1 << 9)

/* smallest block: header + next + prev + footer */
#define MINBLOCK (3 * DSIZE)

/* blocks up to this size are carved from the low end of a free block; larger
 * ones are carved from the high end (see place) */
#define SMALL_MAX 96

/* number of segregated free lists */
#define NUM_CLASSES 72

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

/* the two link fields that live in the payload of a free block */
#define PREV_FREEP(bp) (*(char **)(bp))
#define NEXT_FREEP(bp) (*(char **)((char *)(bp) + DSIZE))

/* heads of the segregated free lists */
static char *free_list[NUM_CLASSES];

/* points at the prologue block payload; the first real block follows it */
static char *heap_listp = NULL;

static void *extend_heap(size_t size);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void *place(void *bp, size_t asize);
static void trim_block(void *bp, size_t asize, size_t total);
static void insert_free(void *bp);
static void remove_free(void *bp);
static size_t adjust(size_t size);

/*
 * class_of - map a block size to a free-list index.
 *
 * Sizes up to 128 bytes get one class each in 8-byte steps (indices 0..13).
 * Above that every power-of-two octave is split into four sub-buckets, so a
 * class spans at most 25% of its size and a best-fit search inside a class
 * is already precise.
 */
static inline int class_of(size_t size) {
    size_t t;
    int k;
    int idx;

    if (size <= 128)
        return (int)((size - MINBLOCK) >> 3);

    /* k = floor(log2(size - 1)), so 2^k < size <= 2^(k+1) */
    t = size - 1;
    k = 0;
    while (t > 1) {
        t >>= 1;
        k++;
    }
    idx = 14 + ((k - 7) << 2) + (int)(((size - 1) >> (k - 2)) & 3);
    return (idx < NUM_CLASSES) ? idx : (NUM_CLASSES - 1);
}

/*
 * insert_free - push a free block onto the head of its size class (LIFO).
 */
static void insert_free(void *bp) {
    int c = class_of(GET_SIZE(HDRP(bp)));

    PREV_FREEP(bp) = NULL;
    NEXT_FREEP(bp) = free_list[c];
    if (free_list[c] != NULL)
        PREV_FREEP(free_list[c]) = bp;
    free_list[c] = bp;
}

/*
 * remove_free - unlink a free block from its size class.
 */
static void remove_free(void *bp) {
    int c = class_of(GET_SIZE(HDRP(bp)));
    char *prev = PREV_FREEP(bp);
    char *next = NEXT_FREEP(bp);

    if (prev != NULL)
        NEXT_FREEP(prev) = next;
    else
        free_list[c] = next;
    if (next != NULL)
        PREV_FREEP(next) = prev;
}

/*
 * adjust - round a request up to a whole block size, never below MINBLOCK.
 */
static size_t adjust(size_t size) {
    size_t asize = (size + DSIZE + (DSIZE - 1)) & ~(size_t)0x7;

    return (asize < MINBLOCK) ? MINBLOCK : asize;
}

/*
 * mm_init - build the empty heap and clear the free lists.
 */
int mm_init(void) {
    int i;

    for (i = 0; i < NUM_CLASSES; i++)
        free_list[i] = NULL;

    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);                            /* alignment padding */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); /* prologue header */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); /* prologue footer */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     /* epilogue header */
    heap_listp += (2 * WSIZE);

    if (extend_heap(CHUNKSIZE) == NULL)
        return -1;
    return 0;
}

/*
 * mm_malloc - allocate at least size bytes, 8-byte aligned.
 */
void *mm_malloc(size_t size) {
    size_t asize;
    char *bp;

    if (size == 0)
        size = 1;

    asize = adjust(size);

    if ((bp = find_fit(asize)) != NULL)
        return place(bp, asize);

    if ((bp = extend_heap(MAX(asize, CHUNKSIZE))) == NULL)
        return NULL;
    remove_free(bp); /* extend_heap hands it back on a free list */
    return place(bp, asize);
}

/*
 * mm_free - free a block and merge it with any free neighbours.
 */
void mm_free(void *ptr) {
    size_t size;

    if (ptr == NULL)
        return;

    size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    insert_free(coalesce(ptr));
}

/*
 * trim_block - finish a grow at bp.  The region starting at bp holds total
 *     bytes and has no live neighbour inside it; turn it into an allocated
 *     block of asize bytes and give the remainder back to the free lists
 *     (coalescing it, because what follows may itself be free).
 */
static void trim_block(void *bp, size_t asize, size_t total) {
    if (total - asize >= MINBLOCK) {
        char *rest;

        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        rest = NEXT_BLKP(bp);
        PUT(HDRP(rest), PACK(total - asize, 0));
        PUT(FTRP(rest), PACK(total - asize, 0));
        insert_free(coalesce(rest));
    } else {
        PUT(HDRP(bp), PACK(total, 1));
        PUT(FTRP(bp), PACK(total, 1));
    }
}

/*
 * mm_realloc - resize a block, staying put whenever the heap allows it.
 */
void *mm_realloc(void *ptr, size_t size) {
    char *bp = ptr;
    char *next;
    char *newptr;
    size_t asize;
    size_t csize;
    size_t oldsize;
    size_t tsize;
    size_t total;

    if (bp == NULL)
        return mm_malloc(size);

    if (size == 0) {
        mm_free(bp);
        return NULL;
    }

    asize = adjust(size);
    csize = GET_SIZE(HDRP(bp));

    /* shrinking: keep the block, hand the tail back to the free lists */
    if (asize <= csize) {
        if (csize - asize >= MINBLOCK)
            trim_block(bp, asize, csize);
        return bp;
    }

    next = NEXT_BLKP(bp);

    /* 1. swallow a free block that follows */
    if (!GET_ALLOC(HDRP(next))) {
        tsize = GET_SIZE(HDRP(next));
        if (csize + tsize >= asize) {
            remove_free(next);
            trim_block(bp, asize, csize + tsize);
            return bp;
        }
        /* 2. the free tail ends the heap: absorb it and take just enough
         *    fresh brk space to reach asize */
        if (GET_SIZE(HDRP(NEXT_BLKP(next))) == 0) {
            char *nb = extend_heap(asize - csize - tsize);

            if (nb != NULL) {
                remove_free(nb); /* extend_heap merged it into the tail */
                trim_block(bp, asize, csize + GET_SIZE(HDRP(nb)));
                return bp;
            }
        }
    }
    /* 3. the block tops the heap: take the new space directly */
    else if (GET_SIZE(HDRP(next)) == 0) {
        char *nb = extend_heap(asize - csize);

        if (nb != NULL) {
            total = csize + GET_SIZE(HDRP(nb));
            remove_free(nb);
            trim_block(bp, asize, total);
            return bp;
        }
    }

    /* 4. slide the payload down into a free block that precedes bp; this
     *    recycles the hole a move would otherwise strand */
    {
        char *prev = PREV_BLKP(bp);

        if (!GET_ALLOC(FTRP(prev))) {
            size_t psize = GET_SIZE(HDRP(prev));

            if (csize + psize >= asize) {
                remove_free(prev);
                oldsize = csize - DSIZE;
                memmove(prev, bp, oldsize);
                trim_block(prev, asize, csize + psize);
                return prev;
            }
        }
    }

    /* 5. last resort: allocate elsewhere, copy, and release the old block */
    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;

    oldsize = csize - DSIZE;
    memcpy(newptr, bp, (size < oldsize) ? size : oldsize);
    mm_free(bp);
    return newptr;
}

/*
 * extend_heap - grow the heap by size bytes (rounded up) and return the
 *     resulting free block, merged with a free predecessor if there is one.
 *     The block is on a free list when this returns.
 */
static void *extend_heap(size_t size) {
    char *bp;

    size = (size + (DSIZE - 1)) & ~(size_t)0x7;
    if (size < MINBLOCK)
        size = MINBLOCK;

    if ((bp = mem_sbrk((int)size)) == (void *)-1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* new epilogue header */

    bp = coalesce(bp);
    insert_free(bp);
    return bp;
}

/*
 * coalesce - merge a free block with any free neighbour and return the
 *     merged block.  The merged block is *not* on a free list yet.
 */
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        return bp;
    } else if (prev_alloc && !next_alloc) {
        char *next = NEXT_BLKP(bp);

        remove_free(next);
        size += GET_SIZE(HDRP(next));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        return bp;
    } else if (!prev_alloc && next_alloc) {
        char *prev = PREV_BLKP(bp);

        remove_free(prev);
        size += GET_SIZE(HDRP(prev));
        bp = prev;
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        return bp;
    } else {
        char *prev = PREV_BLKP(bp);
        char *next = NEXT_BLKP(bp);

        size += GET_SIZE(HDRP(prev)) + GET_SIZE(HDRP(next));
        remove_free(prev);
        remove_free(next);
        bp = prev;
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        return bp;
    }
}

/*
 * find_fit - best fit within the first size class that has a block big
 *     enough.  At most 16 blocks are examined, so an allocation never turns
 *     into a heap scan.  Returns an unlinked block, or NULL.
 */
static void *find_fit(size_t asize) {
    int c;

    for (c = class_of(asize); c < NUM_CLASSES; c++) {
        char *p = free_list[c];
        char *best = NULL;
        size_t bestsize = 0;
        int examined = 0;

        while (p != NULL && examined < 16) {
            size_t bsize = GET_SIZE(HDRP(p));

            if (bsize >= asize) {
                if (best == NULL || bsize < bestsize) {
                    best = p;
                    bestsize = bsize;
                    if (bsize == asize)
                        break;
                }
            }
            p = NEXT_FREEP(p);
            examined++;
        }

        if (best != NULL) {
            remove_free(best);
            return best;
        }
    }
    return NULL;
}

/*
 * place - carve a block of asize bytes out of the free block bp and return
 *     the payload pointer.  bp must not be on a free list.
 *
 *     Small blocks are taken from the low end and leave the remainder above;
 *     large blocks are taken from the high end and leave the remainder
 *     below.  Either way the remainder's neighbours are allocated, so it can
 *     be linked straight into a free list.
 */
static void *place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));

    if (csize - asize >= MINBLOCK) {
        if (asize <= SMALL_MAX) {
            char *rest;

            PUT(HDRP(bp), PACK(asize, 1));
            PUT(FTRP(bp), PACK(asize, 1));
            rest = NEXT_BLKP(bp);
            PUT(HDRP(rest), PACK(csize - asize, 0));
            PUT(FTRP(rest), PACK(csize - asize, 0));
            insert_free(rest);
            return bp;
        } else {
            char *rest = bp;
            char *blk = (char *)bp + (csize - asize);

            PUT(HDRP(rest), PACK(csize - asize, 0));
            PUT(FTRP(rest), PACK(csize - asize, 0));
            PUT(HDRP(blk), PACK(asize, 1));
            PUT(FTRP(blk), PACK(asize, 1));
            insert_free(rest);
            return blk;
        }
    }

    PUT(HDRP(bp), PACK(csize, 1));
    PUT(FTRP(bp), PACK(csize, 1));
    return bp;
}
