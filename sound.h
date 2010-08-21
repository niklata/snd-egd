#ifndef NJK_INCLUDE_SOUND_H_
#define NJK_INCLUDE_SOUND_H_ 1

/*
 * Copyright (C) 2010 Nicholas J. Kain <nicholas aatt kain.us>
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

void sound_open(void);
int sound_bytes_per_frame(void);
int sound_read(void *buf, size_t size);
void sound_start(void);
void sound_stop(void);
void sound_close(void);
int sound_is_le(void);
int sound_is_be(void);
void sound_set_device(char *str);
void sound_set_port(char *str);
void sound_set_sample_rate(int rate);
void sound_set_skip_bytes(int sb);

#endif

