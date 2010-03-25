#ifndef __NJK_UTIL_H_
#define __NJK_UTIL_H_ 1

#include "defines.h"

extern char *pidfile_path;

void write_pidfile(void);
void daemonize(void);
void gracefully_exit(int signum);
void logging_handler(int signum);
void *xmalloc(size_t size);
void dolog(int vopt, int level, char *format, ...);

#endif
