/* wasi-libc's <sys/resource.h> (with -D_WASI_EMULATED_PROCESS_CLOCKS) has getrusage() but neither rlimits
 * nor rusage.ru_maxrss.  qfits_memory.c uses both only for optional bookkeeping. */
#ifndef AN_WASI_SYS_RESOURCE_H
#define AN_WASI_SYS_RESOURCE_H
#include_next <sys/resource.h>
#define ru_maxrss ru_utime.tv_sec
struct rlimit { unsigned long rlim_cur, rlim_max; };
#define RLIMIT_NOFILE 7
static inline int getrlimit(int r, struct rlimit* l) { l->rlim_cur = l->rlim_max = 0; return 0; }
static inline int setrlimit(int r, const struct rlimit* l) { return 0; }
#endif
