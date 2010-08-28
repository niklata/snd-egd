#ifndef NJK_DEFINES_H_
#define NJK_DEFINES_H_ 1

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

#define SNDEGD_VERSION "1.0"

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)>(b)?(b):(a))

#define MAX_PATH_LENGTH 1024
#define MAX_BUF 1024
#define MAXLINE 1024

#define PAGE_SIZE 4096

#define USE_EPOLL 1
#define USE_AMLS 1

#define RANDOM_DEVICE               "/dev/random"
#define DEFAULT_PID_FILE            "/var/run/snd-egd.pid"
#define DEFAULT_HW_DEVICE           "hw:0"
#define DEFAULT_HW_ITEM             "capture"
#define DEFAULT_SAMPLE_RATE         48000
#define DEFAULT_SKIP_BYTES          (48000 * 4 * 1)
#define DEFAULT_MAX_BIT             16
#define DEFAULT_POOLSIZE_FN         "/proc/sys/kernel/random/poolsize"
#define RB_SIZE                     PAGE_SIZE
#define POOL_BUFFER_SIZE            PAGE_SIZE

#endif

