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

#include <stdlib.h>

#include "rb.h"
#include "sound.h"
#include "getrandom.h"
#include "log.h"
#include "util.h"

extern ring_buffer_t rb;
extern unsigned char max_bit;

/* Global for speed... */
static union frame_t vnbuf[PAGE_SIZE / sizeof(union frame_t)];
static vn_renorm_state_t vnstate[2];
static unsigned int stats[2][16][256];

void print_random_stats(void)
{
    log_line(LOG_DEBUG, "LEFT sampled random character counts:");
    log_line(LOG_DEBUG, "byte:\t 1\t 2\t 3\t 4\t 5\t 6\t 7\t 8");
    for (int i = 0; i < 256; ++i) {
        log_line(LOG_DEBUG, "%i:\t %d\t %d\t %d\t %d\t %d\t %d\t %d\t %d", i,
                 stats[0][0][i], stats[0][1][i], stats[0][2][i],
                 stats[0][3][i], stats[0][4][i], stats[0][5][i],
                 stats[0][6][i], stats[0][7][i]);
    }
    log_line(LOG_DEBUG, "byte:\t 9\t 10\t 11\t 12\t 13\t 14\t 15\t 16");
    for (int i = 0; i < 256; ++i) {
        log_line(LOG_DEBUG, "%i:\t %d\t %d\t %d\t %d\t %d\t %d\t %d\t %d", i,
                 stats[0][8][i], stats[0][9][i], stats[0][10][i],
                 stats[0][11][i], stats[0][12][i], stats[0][13][i],
                 stats[0][14][i], stats[0][15][i]);
    }
    log_line(LOG_DEBUG, "RIGHT sampled random character counts:");
    log_line(LOG_DEBUG, "byte:\t 1\t 2\t 3\t 4\t 5\t 6\t 7\t 8\t");
    for (int i = 0; i < 256; ++i) {
        log_line(LOG_DEBUG, "%i:\t %d\t %d\t %d\t %d\t %d\t %d\t %d\t %d", i,
                 stats[1][0][i], stats[1][1][i], stats[1][2][i],
                 stats[1][3][i], stats[1][4][i], stats[1][5][i],
                 stats[1][6][i], stats[1][7][i]);
    }
    log_line(LOG_DEBUG, "byte:\t 9\t 10\t 11\t 12\t 13\t 14\t 15\t 16");
    for (int i = 0; i < 256; ++i) {
        log_line(LOG_DEBUG, "%i:\t %d\t %d\t %d\t %d\t %d\t %d\t %d\t %d", i,
                 stats[1][8][i], stats[1][9][i], stats[1][10][i],
                 stats[1][11][i], stats[1][12][i], stats[1][13][i],
                 stats[1][14][i], stats[1][15][i]);
    }
    log_line(LOG_DEBUG, "total random character counts:");
    for (int i = 0; i < 256; ++i) {
        int outl = 0, outr = 0;
        for (int j = 0; j < 16; ++j) {
            outl += stats[0][j][i];
            outr += stats[1][j][i];
        }
        log_line(LOG_DEBUG, "%i:\t %d\t %d", i, outl, outr);
    }
}

static void vn_renorm_init(void)
{
    int i, j;

    for (i = 0; i < 2; ++i) {
        vnstate[i].total_out = 0;
        vnstate[i].topbit = MIN(max_bit, 16);
        for (j = 0; j < 16; ++j) {
            vnstate[i].bits_out[j] = 0;
            vnstate[i].byte_out[j] = 0;
            vnstate[i].prev_bits[j] = -1;
        }
    }
}

static size_t buf_to_deltabuf(size_t frames)
{
    int i;

    if (frames < 2)
        return 0;

    for (i = 0; i < frames - 1; ++i) {
        vnbuf[i].channel[0] = abs(vnbuf[i+1].channel[0] - vnbuf[i].channel[0]);
        vnbuf[i].channel[1] = abs(vnbuf[i+1].channel[1] - vnbuf[i].channel[1]);
    }

    return frames - 1;
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
static int vn_renorm(uint16_t i, size_t channel)
{
    unsigned int j, new;

    /* process bits */
    for (j = 0; j < vnstate[channel].topbit; ++j) {
        /* Select the bit of given significance. */
        new = (i >> j) & 0x01;

        /* We've not yet collected two bits; move on. */
        if (vnstate[channel].prev_bits[j] == -1) {
            vnstate[channel].prev_bits[j] = new;
            continue;
        }

        /* If the bits are equal, discard both. */
        if (vnstate[channel].prev_bits[j] == new) {
            vnstate[channel].prev_bits[j] = -1;
            continue;
        }

        /* If 10, mark the bit as 1.  Otherwise, it's 01 and the bit
         * is already marked as 0. */
        if (vnstate[channel].prev_bits[j])
            vnstate[channel].byte_out[j] |= 1 << vnstate[channel].bits_out[j];
        vnstate[channel].bits_out[j]++;
        vnstate[channel].prev_bits[j] = -1;

        /* See if we've collected an entire byte.  If so, then copy
         * it into the output buffer. */
        if (vnstate[channel].bits_out[j] == 8) {
            stats[channel][j][vnstate[channel].byte_out[j]] += 1;
            vnstate[channel].total_out +=
                rb_store_byte_xor(&rb, vnstate[channel].byte_out[j]);

            vnstate[channel].bits_out[j] = 0;
            vnstate[channel].byte_out[j] = 0;

            if (rb_is_full(&rb))
                return 1;
        }
    }
    return 0;
}

/* target = desired bytes of entropy that should be retrieved */
void get_random_data(int target)
{
    int total_in = 0, total_out = 0, frames = 0, framesize = 0, i;
    vn_renorm_init();

    log_line(LOG_DEBUG, "get_random_data(%d)", target);

    target = MIN(sizeof vnbuf, target);

    sound_start();
    while (total_out < target) {
        frames = sound_read(vnbuf, target);
        framesize = sound_bytes_per_frame();
        log_line(LOG_DEBUG, "frames = %d", frames);

        frames = buf_to_deltabuf(frames);

        for (i = 0; i < frames; ++i) {
            if (vn_renorm(vnbuf[i].channel[0], 0))
                break;
            if (vn_renorm(vnbuf[i].channel[1], 1))
                break;
        }
        /* may be inaccurate by one 16-bit value, not worth the speed hit
           for precise accounting */
        total_in += i * framesize;
        total_out = vnstate[0].total_out + vnstate[1].total_out;
    }
    sound_stop();

    log_line(LOG_DEBUG, "get_random_data(): in->out bytes = %d->%d, eff = %f",
             total_in, total_out, (float)total_out / (float)total_in);
}
