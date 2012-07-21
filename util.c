/*
 * (c) 2010-2012 Nicholas J. Kain <njkain at gmail dot com>
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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>

#include "log.h"
#include "rb.h"
#include "util.h"
#include "sound.h"

char *pidfile_path = DEFAULT_PID_FILE;
extern ring_buffer_t *rb;

int parse_user(char *username, int *gid)
{
    int t;
    char *p;
    struct passwd *pws;

    t = (unsigned int) strtol(username, &p, 10);
    if (*p != '\0') {
        pws = getpwnam(username);
        if (pws) {
            t = (int)pws->pw_uid;
            if (*gid < 1)
                *gid = (int)pws->pw_gid;
        } else suicide("FATAL - Invalid uid specified.\n");
    }
    return t;
}

int parse_group(char *groupname)
{
    int t;
    char *p;
    struct group *grp;

    t = (unsigned int) strtol(groupname, &p, 10);
    if (*p != '\0') {
        grp = getgrnam(groupname);
        if (grp) {
            t = (int)grp->gr_gid;
        } else suicide("FATAL - Invalid gid specified.\n");
    }
    return t;
}

void write_pidfile(void)
{
    FILE *fh = fopen(pidfile_path, "w");
    if (!fh)
        suicide("failed creating pid file %s", pidfile_path);

    fprintf(fh, "%i", getpid());
    fclose(fh);
}

void daemonize(void)
{
    if (daemon(0, 0) == -1)
        suicide("fork failed");

    write_pidfile();
}

