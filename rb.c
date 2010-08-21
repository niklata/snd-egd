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

#include <stdlib.h>
#include <string.h>
#include "rb.h"
#include "log.h"

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
