// Copyright 2008-2016 Nicholas J. Kain <njkain at gmail dot com>
// SPDX-License-Identifier: MIT
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <linux/random.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/capability.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <grp.h>
#include "nk/log.h"
#include "nk/privs.h"
#include "defines.h"
#include "sound.h"
#include "rb.h"
#include "getrandom.h"

static int signalFd;

static int epollfd;
static struct epoll_event events[2];

bool gflags_debug = 0;

ring_buffer_t rb;

static int refill_timeout = DEFAULT_REFILL_SECS;

struct pool_buffer_t {
    struct rand_pool_info info;
    char buf[POOL_BUFFER_SIZE];
};

static char *chroot_path;

static void exit_cleanup(uint32_t signum)
{
    if (munlockall() == -1)
        suicide("problem unlocking pages");
    sound_close();
    if (signum)
        log_line("snd-egd stopping due to signal %d", signum);
    print_random_stats();
    exit(EXIT_SUCCESS);
}

static void signal_dispatch()
{
    size_t off = 0;
    int t;
    struct signalfd_siginfo si;
  again:
    t = read(signalFd, (char *)&si + off, sizeof si - off);
    if (t < 0) {
        if (t == EAGAIN || t == EWOULDBLOCK || t == EINTR)
            goto again;
        else
            suicide("signalfd read error");
    }
    assert(t >= 0);
    if ((size_t)t < sizeof si - off)
        off += (size_t)t;
    switch (si.ssi_signo) {
        case SIGHUP:
        case SIGINT:
        case SIGTERM:
            exit_cleanup(si.ssi_signo);
            break;
        case SIGUSR1:
            t = gflags_debug;
            gflags_debug = 1;
            print_random_stats();
            gflags_debug = t;
            break;
        case SIGUSR2:
            gflags_debug = !gflags_debug;
        default:
            break;
    }
}

static void epoll_init(int random_fd)
{
    static struct epoll_event ev;
    epollfd = epoll_create1(0);
    if (epollfd == -1)
        suicide("epoll_create1 failed");

    ev.events = EPOLLOUT;
    ev.data.fd = random_fd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, random_fd, &ev) == -1)
        suicide("epoll_ctl failed");
    ev.events = EPOLLIN;
    ev.data.fd = signalFd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, signalFd, &ev) == -1)
        suicide("epoll_ctl failed");
}

static unsigned random_max_bits()
{
    int ret, fd;
    char buf[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    fd = open(DEFAULT_POOLSIZE_FN, O_RDONLY);
    if (!fd)
        suicide("couldn't open poolsize procfs file");
    if (read(fd, buf, sizeof buf - 1) == -1) {
        close(fd);
        suicide("failed to read poolsize procfs file");
    }
    ret = atoi(buf);
    if (ret < 1) {
        close(fd);
        suicide("poolsize can never be less than 1");
    }
    close(fd);
    return (unsigned)ret;
}

static unsigned random_cur_bits(int random_fd)
{
    int ret;
    if (ioctl(random_fd, RNDGETENTCNT, &ret) == -1)
        suicide("Couldn't query entropy-level from kernel");
    return (unsigned)MAX(ret, 0);
}

/*
 * Loads the kernel random number generator with data from the output data
 * arrays.
 * @return number of bits that were loaded to the KRNG
 */
static unsigned int add_entropy(struct pool_buffer_t *poolbuf, int handle,
                                unsigned wanted_bits)
{
    unsigned int total_cur_bytes;
    unsigned int wanted_bytes;

    wanted_bytes = wanted_bits / 8;
    if (wanted_bits & 7)
        ++wanted_bytes;

    total_cur_bytes = rb_num_bytes(&rb);

    if (total_cur_bytes < wanted_bytes)
        wanted_bytes = total_cur_bytes;

    wanted_bytes = MIN(wanted_bytes, POOL_BUFFER_SIZE);
    poolbuf->info.entropy_count = (int)MIN(wanted_bytes * 8, (unsigned)INT_MAX);
    poolbuf->info.buf_size = (int)MIN(wanted_bytes, (unsigned)INT_MAX);
    if (rb_move(&rb, poolbuf->buf, wanted_bytes) == -1)
        suicide("rb_move() failed");

    if (ioctl(handle, RNDADDENTROPY, poolbuf) == -1)
        suicide("RNDADDENTROPY failed!");

    if (gflags_debug) log_line("%d bits requested, %d bits in RB, %d bits added, %d bits left in RB",
              wanted_bits, total_cur_bytes * 8, wanted_bytes * 8, rb_num_bytes(&rb) * 8);

    return wanted_bytes * 8;
}

static void fill_entropy_amount(int random_fd, unsigned max_bits, unsigned wanted_bits)
{
    struct pool_buffer_t poolbuf;

    if (wanted_bits > max_bits)
        wanted_bits = max_bits;

    /*
     * Loop until the buffer is full: we do not check the number of
     * bits currently in the buffer on each iteration, since it
     * might cause snd-egd to run constantly if there are
     * a lot of bytes being consumed from the random device.
     */
    for (unsigned i = 0; i < wanted_bits;)
        i += add_entropy(&poolbuf, random_fd, wanted_bits - i);

    if (rb.bytes < sizeof rb.buf / 4)
        get_random_data(rb.size - rb.bytes);
}

static void fill_entropy(int random_fd, unsigned max_bits)
{
    if (gflags_debug) log_line("woke up due to low entropy state");

    /* Find out how many bits to add */
    unsigned before = random_cur_bits(random_fd);
    if (max_bits <= before) return;

    unsigned wanted_bits = max_bits - before;
    if (gflags_debug) log_line("max_bits: %u, wanted_bits: %u", max_bits, wanted_bits);

    fill_entropy_amount(random_fd, max_bits, wanted_bits);
}

static void refill_timeout_set(int timeout)
{
    refill_timeout = timeout * 1000;
    if (refill_timeout < 0)
        refill_timeout = -1;
}

static bool refill_if_timeout(int random_fd, unsigned max_bits, int timeout)
{
    static struct timespec refill_ts;
    struct timespec curts;
    bool ts_filled = false;

    if (timeout < 0)
        goto out;

    if (clock_gettime(CLOCK_MONOTONIC, &curts))
        suicide("clock_gettime failed");
    time_t secdiff = (curts.tv_sec - refill_ts.tv_sec) * 1000;
    long nsecdiff = (long)secdiff * 1000000L +
                    curts.tv_nsec - refill_ts.tv_nsec;
    if (secdiff >= timeout || nsecdiff >= (long)timeout * 1000000000L) {
        ts_filled = true;
        if (gflags_debug) log_line("timeout: filling with entropy");
        fill_entropy_amount(random_fd, max_bits, max_bits);
    }
    refill_ts.tv_sec = curts.tv_sec;
    refill_ts.tv_nsec = curts.tv_nsec;
out:
    return ts_filled;
}

static void main_loop(int random_fd, unsigned max_bits)
{
    /* Fill up the buffer and refresh any timeout. */
    refill_if_timeout(random_fd, max_bits, 0);

    for (;;) {
        int ret = epoll_wait(epollfd, events, 2, refill_timeout);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            else
                suicide("epoll_wait failed");
        }

        bool ts_filled = refill_if_timeout(random_fd, max_bits,
                                           refill_timeout);

        size_t evmax = (size_t)MAX(ret, 0);
        for (size_t i = 0; i < evmax; ++i) {
            int fd = events[i].data.fd;
            if (fd == signalFd) {
                if (events[i].events & EPOLLIN)
                    signal_dispatch();
            } else if (fd == random_fd) {
                if (events[i].events & EPOLLOUT) {
                    if (!ts_filled)
                        fill_entropy(random_fd, max_bits);
                }
            }
        }
    }
}

static void usage(void)
{
    printf("Collect entropy from a sound card and feed it into the kernel random pool.\n");
    printf("Usage: snd-egd [options]\n\n");
    printf("--device          -d []  Sound device used (default %s)\n", DEFAULT_HW_DEVICE);
    printf("--item            -i []  Sound device item used (default %s)\n", DEFAULT_HW_ITEM);
    printf("--sample-rate     -r []  Audio sampling rate. (default %i)\n", DEFAULT_SAMPLE_RATE);
    printf("--refill-time     -t []  Seconds between full refills (default %i)\n", DEFAULT_REFILL_SECS);
    printf("--skip-bytes      -s []  Ignore first N audio bytes (default %i)\n", DEFAULT_SKIP_BYTES);
    printf("--user            -u []  User name or id to change to after dropping privileges.\n");
    printf("--chroot          -c []  Directory to use as the chroot jail.\n");
    printf("--verbose         -v     Be verbose.\n");
    printf("--help            -h     This help.\n");
}

static void copyright(void)
{
    printf("snd-egd %s, sound entropy gathering daemon.\n", SNDEGD_VERSION);
    printf("Copyright 2008-2020 Nicholas J. Kain\n\n"
"Permission is hereby granted, free of charge, to any person obtaining\n"
"a copy of this software and associated documentation files (the\n"
"\"Software\"), to deal in the Software without restriction, including\n"
"without limitation the rights to use, copy, modify, merge, publish,\n"
"distribute, sublicense, and/or sell copies of the Software, and to\n"
"permit persons to whom the Software is furnished to do so, subject to\n"
"the following conditions:\n\n"
"The above copyright notice and this permission notice shall be\n"
"included in all copies or substantial portions of the Software.\n\n"
"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND,\n"
"EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF\n"
"MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND\n"
"NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE\n"
"LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION\n"
"OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION\n"
"WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n"
           );
}

static void setup_signals()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPIPE);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, (sigset_t *)0) < 0)
        suicide("sigprocmask failed");
    signalFd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (signalFd < 0)
        suicide("signalfd failed");
}

int main(int argc, char **argv)
{
    int c, random_fd = -1;
    uid_t uid;
    gid_t gid;
    bool have_uid = false;
    struct option long_options[] = {
        {"device",  1, (int *)0, 'd'},
        {"port", 1, (int *)0, 'i'},
        {"sample-rate", 1, (int *)0, 'r'},
        {"skip-bytes", 1, (int *)0, 's'},
        {"refill-time", 1, (int *)0, 't'},
        {"user", 1, (int *)0, 'u'},
        {"chroot", 1, (int *)0, 'c'},
        {"verbose", 0, (int *)0, 'v'},
        {"help", 0, (int *)0, 'h'},
        {(const char *)0, 0, (int *)0, 0 }
    };

    /* Process commandline options */
    for (;;) {
        int t;

        c = getopt_long(argc, argv, "d:i:r:s:t:u:c:vh",
                        long_options, (int *)0);
        if (c == -1)
            break;

        switch(c) {
            case 'd':
                sound_set_device(optarg);
                break;

            case 'i':
                sound_set_port(optarg);
                break;

            case 'r':
                t = atoi(optarg);
                sound_set_sample_rate(t);
                break;

            case 's':
                t = atoi(optarg);
                sound_set_skip_bytes(t);
                break;

            case 't':
                t = atoi(optarg);
                refill_timeout_set(t);
                break;

            case 'u':
                if (nk_uidgidbyname(optarg, &uid, &gid))
                    suicide("invalid user '%s' specified", optarg);
                have_uid = true;
                break;

            case 'c':
                chroot_path = strdup(optarg);
                break;

            case 'v':
                gflags_debug = 1;
                break;

            case 'h':
            default:
                copyright();
                usage();
                exit(EXIT_FAILURE);
        }
    }

    log_line("snd-egd starting up");

    /* Open kernel random device */
    random_fd = open(RANDOM_DEVICE, O_RDWR);
    if (random_fd == -1)
        suicide("Couldn't open random device: %s", strerror(errno));

    /* Find out the kernel entropy pool size */
    unsigned max_bits = random_max_bits(random_fd);

    setup_signals();

    sound_open();

    if (chroot_path)
        nk_set_chroot(chroot_path);
    unsigned char keepcaps[] = { CAP_SYS_ADMIN };
    if (have_uid)
        nk_set_uidgid(uid, gid, keepcaps, sizeof keepcaps);

    if (mlockall(MCL_FUTURE))
        suicide("mlockall failed");

    rb_init(&rb);
    vn_buf_lock();
    epoll_init(random_fd);

    /* Prefill entropy buffer */
    get_random_data(rb.size - rb.bytes);

    main_loop(random_fd, max_bits);

    exit_cleanup(0);
}
