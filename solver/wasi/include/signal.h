/* wasi-libc has no sigaction().  ioutils.c installs a SIGBUS handler around mmap()ed files; with the
 * emulated mmap (a plain read into memory) a bus error cannot happen, so installing it is a no-op. */
#ifndef AN_WASI_SIGNAL_H
#define AN_WASI_SIGNAL_H
#include_next <signal.h>
struct sigaction { void (*sa_handler)(int); unsigned long sa_mask; int sa_flags; };
static inline int sigaction(int s, const struct sigaction* n, struct sigaction* o) { return 0; }
#endif
