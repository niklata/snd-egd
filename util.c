#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "log.h"
#include "rb.h"
#include "util.h"

char *pidfile_path = DEFAULT_PID_FILE;
extern ring_buffer_t *rb;

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

void gracefully_exit(int signum)
{
    if (munlockall() == -1)
        suicide("problem unlocking pages");
    unlink(pidfile_path);
    rb_delete(rb);
    log_line(LOG_NOTICE, "snd-egd stopping due to signal %d", signum);
    exit(0);
}

void logging_handler(int signum)
{
    if (signum == SIGUSR1)
        gflags_debug = 1;

    if (signum == SIGUSR2)
        gflags_debug = 0;
}

void *xmalloc(size_t size)
{
    void *ret;

    ret = malloc(size);
    if (!ret)
        suicide("FATAL - malloc() failed");
    return ret;
}

