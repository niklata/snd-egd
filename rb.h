#ifndef NK_RING_BUFFER_H_
#define NK_RING_BUFFER_H_ 1

/*
 * Copyright (C) 2010 Nicholas J. Kain <nicholas aatt kain.us>
 *
 * Simple ring buffer specialized for gathering entropy.  It's not quite
 * general since it assumes that the only necessary distinction between
 * data within the buffer is filled or unfilled, with no regards to
 * actual ordering.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/mman.h>

#include "defines.h"
#include "string.h"

typedef struct {
    char buf[RB_SIZE];
    unsigned int size; /* max size of the buffer in bytes */
    unsigned int bytes; /* current size of the buffer in bytes */
    unsigned int index;
    unsigned int fill_idx;
} ring_buffer_t;

/* creates a new, empty ring buffer */
static inline void rb_init(ring_buffer_t *rb)
{
    rb->size = RB_SIZE;
    rb->index = RB_SIZE;
    rb->fill_idx = 0;
    rb->bytes = 0;
    mlock(rb->buf, RB_SIZE);
    memset(rb->buf, '\0', RB_SIZE);
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
/* returns 1 if store successful, otherwise 0 (error or not enough room) */
unsigned int rb_store_byte_xor(ring_buffer_t *rb, char b);
/* returns 0 on success, a negative number if not enough bytes or error */
int rb_move(ring_buffer_t *rb, char *buf, unsigned int bytes);

#endif
