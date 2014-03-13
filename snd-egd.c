/*
 * (c) 2008-2013 Nicholas J. Kain <njkain at gmail dot com>
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
#include <stdbool.h>
#include <getopt.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <linux/random.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/capability.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <grp.h>

#include "defines.h"
#include "log.h"
#include "util.h"
#include "sound.h"
#include "rb.h"
#include "getrandom.h"
#include "seccomp-bpf.h"

static int signalFd;

static int epollfd;
static struct epoll_event events[2];

ring_buffer_t rb;

static int refill_timeout = DEFAULT_REFILL_SECS;

struct pool_buffer_t {
    struct rand_pool_info info;
    char buf[POOL_BUFFER_SIZE];
};

static char *chroot_path;

static int enforce_seccomp(void)
{
    struct sock_filter filter[] = {
        VALIDATE_ARCHITECTURE,
        EXAMINE_SYSCALL,
        ALLOW_SYSCALL(epoll_wait),
        ALLOW_SYSCALL(read),
        ALLOW_SYSCALL(write),
        ALLOW_SYSCALL(clock_gettime),
        ALLOW_SYSCALL(close),
        ALLOW_SYSCALL(ioctl),

        // for glibc syslog
        ALLOW_SYSCALL(sendto),
        ALLOW_SYSCALL(open),
        ALLOW_SYSCALL(fstat),
        ALLOW_SYSCALL(socket),
        ALLOW_SYSCALL(connect),
        ALLOW_SYSCALL(recvmsg),
        ALLOW_SYSCALL(getsockname),

        ALLOW_SYSCALL(munlockall),
        ALLOW_SYSCALL(unlink),
        ALLOW_SYSCALL(munmap),

        ALLOW_SYSCALL(rt_sigreturn),
#ifdef __NR_sigreturn
        ALLOW_SYSCALL(sigreturn),
#endif
        // Allowed by vDSO
        ALLOW_SYSCALL(getcpu),
        ALLOW_SYSCALL(time),
        ALLOW_SYSCALL(gettimeofday),
        ALLOW_SYSCALL(clock_gettime),

        ALLOW_SYSCALL(exit_group),
        ALLOW_SYSCALL(exit),
        KILL_PROCESS,
    };
    struct sock_fprog prog = {
        .len = (unsigned short)(sizeof filter / sizeof filter[0]),
        .filter = filter,
    };
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
        return -1;
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog))
        return -1;
    return 0;
}

static void exit_cleanup(int signum)
{
    if (munlockall() == -1)
        suicide("problem unlocking pages");
    unlink(pidfile_path);
    sound_close();
    if (signum)
        log_line(LOG_NOTICE, "snd-egd stopping due to signal %d", signum);
    print_random_stats();
    exit(0);
}

static void drop_privs(int uid, int gid)
{
    cap_t caps;
    prctl(PR_SET_KEEPCAPS, 1);
    caps = cap_from_text("cap_sys_admin=ep");
    if (!caps)
        suicide("cap_from_text failed");
    if (setgroups(0, NULL) == -1)
        suicide("setgroups failed");
    if (setegid(gid) == -1 || seteuid(uid) == -1)
        suicide("dropping privs failed");
    if (cap_set_proc(caps) == -1)
        suicide("cap_set_proc failed");
    cap_free(caps);
}

static void signal_dispatch()
{
    int t, off = 0;
    struct signalfd_siginfo si;
  again:
    t = read(signalFd, (char *)&si + off, sizeof si - off);
    if (t < sizeof si - off) {
        if (t < 0) {
            if (t == EAGAIN || t == EWOULDBLOCK || t == EINTR)
                goto again;
            else
                suicide("signalfd read error");
        }
        off += t;
    }
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

static int random_max_bits()
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
    return ret;
}

static int random_cur_bits(int random_fd)
{
    int ret;

    if (ioctl(random_fd, RNDGETENTCNT, &ret) == -1)
        suicide("Couldn't query entropy-level from kernel");
    return ret;
}

/*
 * Loads the kernel random number generator with data from the output data
 * arrays.
 * @return number of bits that were loaded to the KRNG
 */
static unsigned int add_entropy(struct pool_buffer_t *poolbuf, int handle,
                                int wanted_bits)
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
    poolbuf->info.entropy_count = wanted_bytes * 8;
    poolbuf->info.buf_size = wanted_bytes;
    if (rb_move(&rb, poolbuf->buf, wanted_bytes) == -1)
        suicide("rb_move() failed");

    if (ioctl(handle, RNDADDENTROPY, poolbuf) == -1)
        suicide("RNDADDENTROPY failed!");

    log_line(LOG_DEBUG, "%d bits requested, %d bits in RB, %d bits added, %d bits left in RB",
             wanted_bits, total_cur_bytes * 8, wanted_bytes * 8, rb_num_bytes(&rb) * 8);

    return wanted_bytes * 8;
}

static void fill_entropy_amount(int random_fd, int max_bits, int wanted_bits)
{
    int i;
    struct pool_buffer_t poolbuf;

    if (wanted_bits > max_bits)
        wanted_bits = max_bits;

    /*
     * Loop until the buffer is full: we do not check the number of
     * bits currently in the buffer on each iteration, since it
     * might cause snd-egd to run constantly if there are
     * a lot of bytes being consumed from the random device.
     */
    for (i = 0; i < wanted_bits;)
        i += add_entropy(&poolbuf, random_fd, wanted_bits - i);

    if (rb.bytes < sizeof rb.buf / 4)
        get_random_data(rb.size - rb.bytes);
}

static void fill_entropy(int random_fd, int max_bits)
{
    int before, wanted_bits;

    log_line(LOG_DEBUG, "woke up due to low entropy state");

    /* Find out how many bits to add */
    before = random_cur_bits(random_fd);
    wanted_bits = max_bits - before;
    log_line(LOG_DEBUG, "max_bits: %d, wanted_bits: %d",
             max_bits, wanted_bits);

    fill_entropy_amount(random_fd, max_bits, wanted_bits);
}

static void refill_timeout_set(int timeout)
{
    refill_timeout = timeout * 1000;
    if (refill_timeout < 0)
        refill_timeout = -1;
}

static bool refill_if_timeout(int random_fd, int max_bits, int timeout)
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
        log_line(LOG_DEBUG, "timeout: filling with entropy");
        fill_entropy_amount(random_fd, max_bits, max_bits);
    }
    refill_ts.tv_sec = curts.tv_sec;
    refill_ts.tv_nsec = curts.tv_nsec;
out:
    return ts_filled;
}

static void main_loop(int random_fd, int max_bits)
{
    /* Fill up the buffer and refresh any timeout. */
    refill_if_timeout(random_fd, max_bits, 0);

    for (;;) {
        int ret = epoll_wait(epollfd, events, 2, refill_timeout);
        if (ret == -1) {
            if (errno == EINTR)
                continue;
            else
                suicide("epoll_wait failed");
        }
        if (ret == -1)
            suicide("epoll_wait failed");

        bool ts_filled = refill_if_timeout(random_fd, max_bits,
                                           refill_timeout);

        for (int i = 0; i < ret; ++i) {
            int fd = events[i].data.fd;
            if (fd == signalFd) {
                signal_dispatch();
            } else if (fd == random_fd) {
                if (!ts_filled)
                    fill_entropy(random_fd, max_bits);
            }
        }
    }
}

static void usage(void)
{
    printf("Collect entropy from a sound card and feed it into the kernel random pool.\n");
    printf("Usage: snd-egd [options]\n\n");
    printf("--device,       -d []  Sound device used (default %s)\n", DEFAULT_HW_DEVICE);
    printf("--item,         -i []  Sound device item used (default %s)\n", DEFAULT_HW_ITEM);
    printf("--sample-rate,  -r []  Audio sampling rate. (default %i)\n", DEFAULT_SAMPLE_RATE);
    printf("--refill-time   -t []  Seconds between full refills (default %i)\n", DEFAULT_REFILL_SECS);
    printf("--skip-bytes,   -s []  Ignore first N audio bytes (default %i)\n", DEFAULT_SKIP_BYTES);
    printf("--pid-file,     -p []  PID file path (default %s)\n", DEFAULT_PID_FILE);
    printf("--user          -u []  User name or id to change to after dropping privileges.\n");
    printf("--group         -g []  Group name or id to change to after dropping privileges.\n");
    printf("--chroot        -c []  Directory to use as the chroot jail.\n");
    printf("--nodetach      -n     Do not fork.\n");
    printf("--verbose,      -v     Be verbose.\n");
    printf("--help,         -h     This help.\n");
}

static void copyright(void)
{
    printf("snd-egd %s, sound entropy gathering daemon.\n", SNDEGD_VERSION);
    printf("Copyright (c) 2008-2013 Nicholas J. Kain\n"
           "All rights reserved.\n\n"
           "Redistribution and use in source and binary forms, with or without\n"
           "modification, are permitted provided that the following conditions are met:\n\n"
           "- Redistributions of source code must retain the above copyright notice,\n"
           "  this list of conditions and the following disclaimer.\n"
           "- Redistributions in binary form must reproduce the above copyright notice,\n"
           "  this list of conditions and the following disclaimer in the documentation\n"
           "  and/or other materials provided with the distribution.\n\n"
           "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\"\n"
           "AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE\n"
           "IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\n"
           "ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE\n"
           "LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR\n"
           "CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF\n"
           "SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS\n"
           "INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN\n"
           "CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)\n"
           "ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE\n"
           "POSSIBILITY OF SUCH DAMAGE.\n");
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
    sigaddset(&mask, SIGUSR2);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
        suicide("sigprocmask failed");
    signalFd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (signalFd < 0)
        suicide("signalfd failed");
}

int main(int argc, char **argv)
{
    int c, random_fd = -1, uid = -1, gid = -1;
    struct option long_options[] = {
        {"device",  1, NULL, 'd'},
        {"port", 1, NULL, 'i'},
        {"nodetach", 1, NULL, 'n'},
        {"sample-rate", 1, NULL, 'r'},
        {"skip-bytes", 1, NULL, 's'},
        {"refill-time", 1, NULL, 't'},
        {"pid-file", 1, NULL, 'p'},
        {"user", 1, NULL, 'u'},
        {"group", 1, NULL, 'g'},
        {"chroot", 1, NULL, 'c'},
        {"verbose", 0, NULL, 'v'},
        {"help", 0, NULL, 'h'},
        {NULL, 0, NULL, 0 }
    };

    /* Process commandline options */
    while (1) {
        int t;

        c = getopt_long(argc, argv, "d:i:nr:s:t:p:u:g:c:vh",
                        long_options, NULL);
        if (c == -1)
            break;

        switch(c) {
            case 'd':
                sound_set_device(optarg);
                break;

            case 'i':
                sound_set_port(optarg);
                break;

            case 'n':
                gflags_detach = 0;
                break;

            case 's':
                t = atoi(optarg);
                sound_set_skip_bytes(t);
                break;

            case 'r':
                t = atoi(optarg);
                sound_set_sample_rate(t);
                break;

            case 't':
                t = atoi(optarg);
                refill_timeout_set(t);
                break;

            case 'p':
                pidfile_path = strdup(optarg);
                break;

            case 'u':
                uid = parse_user(optarg, &gid);
                break;

            case 'g':
                gid = parse_group(optarg);
                break;

            case 'c':
                chroot_path = strdup(optarg);
                break;

            case 'v':
                gflags_debug = 1;
                break;

            case 'h':
            case '?':
            default:
                copyright();
                usage();
                exit(1);
        }
    }

    log_line(LOG_NOTICE, "snd-egd starting up");

    /* Open kernel random device */
    random_fd = open(RANDOM_DEVICE, O_RDWR);
    if (random_fd == -1)
        suicide("Couldn't open random device: %m");

    /* Find out the kernel entropy pool size */
    int max_bits = random_max_bits(random_fd);

    if (gflags_detach)
        daemonize();

    setup_signals();

    sound_open();

    if (chroot_path) {
        if (chdir(chroot_path))
            suicide("chdir chroot failed");
        if (chroot(chroot_path))
            suicide("chroot failed");
    }

    if (uid != -1 && gid != -1)
        drop_privs(uid, gid);

    if (mlockall(MCL_FUTURE))
        suicide("mlockall failed");

    rb_init(&rb);
    vn_buf_lock();
    epoll_init(random_fd);
    if (enforce_seccomp())
        log_line(LOG_NOTICE, "seccomp filter cannot be installed");

    /* Prefill entropy buffer */
    get_random_data(rb.size - rb.bytes);

    main_loop(random_fd, max_bits);

    exit_cleanup(0);
}
