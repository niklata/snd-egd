#ifndef NJK_UTIL_H_
#define NJK_UTIL_H_ 1

#include "defines.h"

extern char *pidfile_path;

static inline int16_t endian_swap16(int16_t val)
{
    return val = (val >> 8) | (val << 8);
}

int parse_user(char *username, int *gid);
int parse_group(char *groupname);
void write_pidfile(void);
void daemonize(void);

#endif
