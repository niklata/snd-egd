#ifndef NK_RING_BUFFER_H_
#define NK_RING_BUFFER_H_ 1
/*
 * (c) 2010-2012 Nicholas J. Kain <njkain at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Simple ring buffer specialized for gathering entropy.  It's not quite
 * general since it assumes that the only necessary distinction between
 * data within the buffer is filled or unfilled, with no regards to
 * actual ordering.
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
