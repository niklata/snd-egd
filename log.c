/*
 * (c) 2005-2012 Nicholas J. Kain <njkain at gmail dot com>
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


#include <stdio.h>
#include <strings.h>
#include <stdarg.h>
#include <stdlib.h>

#include "defines.h"
#include "log.h"

/* global logging flags */
char gflags_detach = 0;
char gflags_debug = 0;

void log_line(int logtype, char *format, ...) {
    va_list argp;

    if (logtype == LOG_DEBUG && !gflags_debug)
        return;

    if (gflags_detach) {
        openlog("snd-egd", LOG_PID, LOG_DAEMON);
        va_start(argp, format);
        vsyslog(logtype | LOG_DAEMON, format, argp);
        va_end(argp);
        closelog();
    } else {
        va_start(argp, format);
        vfprintf(stderr, format, argp);
        fprintf(stderr, "\n");
        va_end(argp);
    }
}

void suicide(char *format, ...) {
    va_list argp;

    if (gflags_detach) {
        openlog("snd-egd", LOG_PID, LOG_DAEMON);
        va_start(argp, format);
        vsyslog(LOG_ERR | LOG_DAEMON, format, argp);
        va_end(argp);
        closelog();
    } else {
        va_start(argp, format);
        vfprintf(stderr, format, argp);
        fprintf(stderr, "\n");
        va_end(argp);
        perror(NULL);
    }
    exit(EXIT_FAILURE);
}

