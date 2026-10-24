/* Declarations for POSIX functions wasi-libc hides (its headers guard them with __wasilibc_unmodified_upstream).
 * Force-included into every file of the build (-include), so the sources stay untouched.  Definitions: compat.c. */
#ifndef AN_WASI_COMPAT_H
#define AN_WASI_COMPAT_H
#include <sys/types.h>

/* process control: not possible under WASI, compat.c fails these with ENOSYS */
pid_t fork(void);
int execlp(const char* file, const char* arg, ...);
pid_t waitpid(pid_t pid, int* status, int options);
#define WIFEXITED(s)   1
#define WEXITSTATUS(s) ((s) & 0xff)
#define WIFSIGNALED(s) 0
#define WTERMSIG(s)    0

/* temporary files */
int mkstemp(char* tmpl);
char* mkdtemp(char* tmpl);
#endif
