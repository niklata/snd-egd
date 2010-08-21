/*
 * Copyright (C) 2008-2010 Nicholas J. Kain <nicholas aatt kain.us>
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

#include "rb.h"
#include "sound.h"
#include "vn.h"
#include "util.h"

extern ring_buffer_t rb;
extern unsigned int stats[2][256];
extern unsigned char max_bit;

void vn_renorm_init(vn_renorm_state_t *state)
{
    int j;

    if (!state)
        return;
    state->bits_out = 0;
    state->byte_out = 0;
    state->total_out = 0;
    state->stats = 0;
    state->topbit = MIN(max_bit, 16);
    for (j = 0; j < 16; ++j)
        state->prev_bits[j] = -1;
}

static int vn_renorm(vn_renorm_state_t *state, uint16_t i)
{
    unsigned int j, new;

    /* process bits */
    for (j = 0; j < state->topbit; ++j) {
        /* Select the bit of given significance. */
        new = (i >> j) & 0x01;
        /* log_line(LOG_DEBUG, "j=%d, prev_bits[j]=%d, new=%d", j, state->prev_bits[j], new); */

        /* We've not yet collected two bits; move on. */
        if (state->prev_bits[j] == -1) {
            state->prev_bits[j] = new;
            continue;
        }

        /* If the bits are equal, discard both. */
        if (state->prev_bits[j] == new) {
            state->prev_bits[j] = -1;
            continue;
        }

        /* If 10, mark the bit as 1.  Otherwise, it's 01 and the bit
         * is already marked as 0. */
        if (state->prev_bits[j])
            state->byte_out |= 1 << state->bits_out;
        state->bits_out++;
        state->prev_bits[j] = -1;

        /* See if we've collected an entire byte.  If so, then copy
         * it into the output buffer. */
        if (state->bits_out == 8) {
            stats[state->stats][state->byte_out] += 1;
            state->total_out += rb_store_byte_xor(&rb, state->byte_out);

            state->bits_out = 0;
            state->byte_out = 0;

            if (rb_is_full(&rb))
                return 1;
        }
    }
    return 0;
}

/*
 * We assume that the chance of a given bit in a sample being a 0 or 1 is not
 * equal.  It is thus a statistically unfair 'coin'.  We can nevertheless use
 * this sample as if it were a fair 'coin' if we use a special procedure:
 *
 * 1. Take a pair of bits of fixed significance in the output
 * 2. If 01, treat as a zero bit.
 * 3. If 10, treat as a one bit.
 * 4. Otherwise, discard as no result.
 *
 * @return number of bytes that were added to the entropy buffer
 */
int vn_renorm_buf(uint16_t buf[], size_t bufsize, vn_renorm_state_t *state)
{
    size_t i;

    if (!buf || !bufsize)
        return 0;

#ifdef HOST_ENDIAN_BE
    if (sound_is_le()) {
#else
    if (sound_is_be()) {
#endif
        for (i = 0; i < bufsize; ++i)
            buf[i] = endian_swap16(buf[i]);
    }

    /* Step through each 16-bit sample in the buffer one at a time. */
    for (i = 0; i < bufsize; ++i)
        vn_renorm(state, buf[i]);
    return state->total_out;
}
