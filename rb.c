/*
 * (c) 2010-2014 Nicholas J. Kain <njkain at gmail dot com>
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

#include <stdlib.h>
#include <string.h>
#include "nk/log.h"
#include "rb.h"

/* returns 1 if store successful, otherwise 0 (error or not enough room) */
unsigned int rb_store_byte(ring_buffer_t *rb, char b)
{
    if (!rb)
        return 0;

    /* Filling in space before the current index. */
    if (rb->fill_idx < rb->index) {
        rb->buf[rb->fill_idx++] = b;
        rb->bytes++;
        return 1;
    } else if (rb->fill_idx > rb->index) {
        if (rb->fill_idx < rb->size) {
            /* Filling in space after the current index. */
            rb->buf[rb->fill_idx++] = b;
            rb->bytes++;
            return 1;
        } else {
            if (rb->index > 0) {
                rb->fill_idx = 0;
                rb->buf[rb->fill_idx++] = b;
                rb->bytes++;
                return 1;
            }
        }
    }
    return 0;
}

/* returns 1 if store successful, otherwise 0 (error or not enough room) */
unsigned int rb_store_byte_xor(ring_buffer_t *rb, char b)
{
    if (!rb)
        return 0;

    /* Filling in space before the current index. */
    if (rb->fill_idx < rb->index) {
        rb->buf[rb->fill_idx++] ^= b;
        rb->bytes++;
        return 1;
    } else if (rb->fill_idx > rb->index) {
        if (rb->fill_idx < rb->size) {
            /* Filling in space after the current index. */
            rb->buf[rb->fill_idx++] ^= b;
            rb->bytes++;
            return 1;
        } else {
            if (rb->index > 0) {
                rb->fill_idx = 0;
                rb->buf[rb->fill_idx++] ^= b;
                rb->bytes++;
                return 1;
            }
        }
    }
    return 0;
}
/* returns 0 on success, a negative number if not enough bytes or error */
int rb_move(ring_buffer_t *rb, char *buf, unsigned int bytes)
{
    unsigned int cbytes;

    if (!bytes)
        return 0;

    if (!rb)
        return -2;
    if (rb->bytes < bytes)
        return -1;

    if (rb->fill_idx < rb->index) {
        /* We're filling the buffer behind the current index. */
        if (rb->index < rb->size) {
            cbytes = MIN(bytes, rb->size - rb->index);
            memcpy(buf, rb->buf + rb->index, cbytes);
            bytes -= cbytes;
            rb->index += cbytes;
            rb->bytes -= cbytes;
            if (bytes > 0) {
                rb->index = 0;
                return rb_move(rb, buf + cbytes, bytes);
            }
            return 0;
        } else {
            rb->index = 0;
            return rb_move(rb, buf, bytes);
        }

    } else if (rb->fill_idx > rb->index) {
        /* We're filling the buffer after the current index. */
        cbytes = MIN(bytes, rb->fill_idx - rb->index);
        memcpy(buf, rb->buf + rb->index, cbytes);
        bytes -= cbytes;
        rb->index += cbytes;
        rb->bytes -= cbytes;
        if (bytes > 0)
            suicide("Ring buffer hit a state that should never happen.");
        return 0;
    } else {
        /* rb->index == rb->fill_idx */
        if (rb->bytes != rb->size)
            suicide("Ring buffer hit a state that should never happen 2.");
        rb->index = 0;
        rb->fill_idx = rb->size;
        return rb_move(rb, buf, bytes);
    }
}
