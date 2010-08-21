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

