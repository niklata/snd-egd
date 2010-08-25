#ifndef GETRANDOM_H_
#define GETRANDOM_H_

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

#include <stdint.h>

/*
 * Frames are 32-bits in length with 16-bits per channel, and each channel
 * word stored in sequence.
 */
union frame_t {
    int16_t channel[2];
    int32_t frame;
};

typedef struct {
    int total_out;
    int bits_out[16];
    char prev_bits[16];
    unsigned char byte_out[16];
} vn_renorm_state_t;

void vn_buf_lock(void);
void print_random_stats(void);
void get_random_data(int target);

#endif /* GETRANDOM_H_ */
