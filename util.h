#ifndef NJK_UTIL_H_
#define NJK_UTIL_H_ 1

#include "defines.h"

extern char *pidfile_path;

static inline int16_t endian_swap16(int16_t *val)
{
    return *val = (*val >> 8) | (*val << 8);
}

void write_pidfile(void);
void daemonize(void);
void gracefully_exit(int signum);
void logging_handler(int signum);
void *xmalloc(size_t size);
void dolog(int vopt, int level, char *format, ...);

#endif
