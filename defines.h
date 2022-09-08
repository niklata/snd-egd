// Copyright 2008-2012 Nicholas J. Kain <njkain at gmail dot com>
// SPDX-License-Identifier: MIT
#ifndef NJK_DEFINES_H_
#define NJK_DEFINES_H_ 1

#define SNDEGD_VERSION "1.2"

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)>(b)?(b):(a))

#define MAX_PATH_LENGTH 1024
#define MAX_BUF 1024
#define MAXLINE 1024

#define PAGE_SIZE 4096

#define USE_AMLS 1

#define RANDOM_DEVICE               "/dev/random"
#define DEFAULT_PID_FILE            "/var/run/snd-egd.pid"
#define DEFAULT_HW_DEVICE           "hw:0"
#define DEFAULT_HW_ITEM             "capture"
#define DEFAULT_SAMPLE_RATE         48000
#define DEFAULT_SKIP_BYTES          (48000 * 4 * 1)
#define DEFAULT_MAX_BIT             16
#define DEFAULT_POOLSIZE_FN         "/proc/sys/kernel/random/poolsize"
#define DEFAULT_REFILL_SECS         60
#define RB_SIZE                     PAGE_SIZE
#define POOL_BUFFER_SIZE            PAGE_SIZE

#endif

