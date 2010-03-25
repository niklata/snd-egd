#ifndef _NK_RING_BUFFER_H_
#define _NK_RING_BUFFER_H_ 1

#include "defines.h"

/*
 * Copyright 2010 Nicholas Kain <nicholas aatt kain.us>
 * Licensed under GPLv3 or later.
 *
 * Simple ring buffer specialized for gathering entropy.  It's not quite
 * general since it assumes that the only necessary distinction between
 * data within the buffer is filled or unfilled, with no regards to
 * actual ordering.
 */

typedef struct {
    char *buf;
    unsigned int size;
    unsigned int bytes;
    unsigned int index;
    unsigned int fill_idx;
} ring_buffer_t;

/* creates a new, empty ring buffer */
static inline ring_buffer_t *rb_new(unsigned int size)
{
    ring_buffer_t *rb = malloc(sizeof (ring_buffer_t));
    rb->size = size;
    rb->index = size;
    rb->fill_idx = 0;
    rb->bytes = 0;
    rb->buf = malloc(size);
    return rb;
}

/* deletes the ring buffer */
static inline void rb_delete(ring_buffer_t *rb)
{
    if (!rb)
        return;
    free(rb->buf);
    free(rb);
}

/* returns number of bytes stored in the ring buffer */
static inline unsigned int rb_num_bytes(ring_buffer_t *rb)
{
    if (!rb)
        return 0;

    return rb->bytes;
}

/* returns 1 if the ring buffer is full or 0 if it is not full */
static inline int rb_is_full(ring_buffer_t *rb)
{
    if (!rb || rb->bytes >= rb->size)
        return 1;
    else
        return 0;
}

/* returns 1 if store successful, otherwise 0 (error or not enough room) */
unsigned int rb_store_byte(ring_buffer_t *rb, char b);
/* returns 0 on success, a negative number if not enough bytes or error */
int rb_move(ring_buffer_t *rb, char *buf, unsigned int bytes);

#endif
