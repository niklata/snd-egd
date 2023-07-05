// Copyright 2003-2012 Nicholas J. Kain <njkain at gmail dot com>
// SPDX-License-Identifier: MIT
#ifndef GETRANDOM_H_
#define GETRANDOM_H_
#include <stdint.h>

/*
 * Frames are 32-bits in length with 16-bits per channel, and each channel
 * word stored in sequence.
 */
struct frame_t {
    int16_t channel[2];
};

typedef struct {
    unsigned int total_out;
    int bits_out[16];
    char prev_bits[16];
    unsigned char byte_out[16];
#ifdef USE_AMLS
    int amls_bits_out[2][16];
    char amls_bits[2][16];
    unsigned char amls_byte_out[2][16];
#endif
} vn_renorm_state_t;

void vn_buf_lock(void);
void print_random_stats(void);
void get_random_data(unsigned target);

#endif
