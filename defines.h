#ifndef __NJK_DEFINES_H_
#define __NJK_DEFINES_H_ 1

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)>(b)?(b):(a))

#define MAX_PATH_LENGTH 1024
#define MAX_BUF 1024
#define MAXLINE 1024

#define RANDOM_DEVICE               "/dev/random"
#define DEFAULT_PID_FILE            "/var/run/snd-egd.pid"
#define DEFAULT_HW_DEVICE           "hw:0"
#define DEFAULT_HW_ITEM             "capture"
#define DEFAULT_SAMPLE_RATE         48000
#define MAX_SAMPLE_SIZE             12000
#define DEFAULT_SKIP_SAMPLES        0
#define DEFAULT_MAX_BIT             16
#define DEFAULT_POOLSIZE_FN         "/proc/sys/kernel/random/poolsize"
#define RB_SIZE                     4096

#endif

