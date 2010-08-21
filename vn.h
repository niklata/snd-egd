#ifndef VN_H_
#define VN_H_

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

typedef struct {
    int bits_out, topbit, total_out;
    int stats;
    char prev_bits[16];
    unsigned char byte_out;
} vn_renorm_state_t;

void vn_renorm_init(vn_renorm_state_t *state);
int vn_renorm_buf(uint16_t buf[], size_t bufsize, vn_renorm_state_t *state);

#endif /* VN_H_ */
