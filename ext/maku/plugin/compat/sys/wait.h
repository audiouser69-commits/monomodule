/* SPDX-License-Identifier: GPL-3.0-only
 * Windows (MinGW) build of the MAKU library: the host tests' process calls (tests/hostsim.c forks its renders) are
 * never reached by the plugin; these stand-ins only let that file compile. */
#ifndef MK_COMPAT_SYS_WAIT_H
#define MK_COMPAT_SYS_WAIT_H
#include <unistd.h>
#define WIFEXITED(s) 1
#define WEXITSTATUS(s) 0
static inline int fork(void) { return -1; }
static inline int pipe(int *fd) { (void)fd; return -1; }
static inline int waitpid(int pid, int *st, int opt) { (void)pid; (void)st; (void)opt; return -1; }
#endif
