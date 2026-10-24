/* The few functions the astrometry.net programs reference that wasi-libc does not provide.
 * Everything else (mmap, fchmod, pipe, dup2, ...) comes from wasi-libc. */
#include "compat.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include "astrometry/image2xy-files.h"

/* Spawning helper programs is not possible under WASI. */
pid_t fork(void)                                  { errno = ENOSYS; return -1; }
int execlp(const char* f, const char* a, ...)     { errno = ENOSYS; return -1; }
pid_t waitpid(pid_t p, int* s, int o)             { errno = ENOSYS; return -1; }

/* wasi-libc declares mkstemp() but does not implement it; ioutils.c uses it for temporary files. */
int mkstemp(char* t) {
    static unsigned n;
    size_t len = strlen(t);
    if (len < 6 || strcmp(t + len - 6, "XXXXXX")) { errno = EINVAL; return -1; }
    for (int tries = 0; tries < 1000; tries++) {
        unsigned v = ++n * 2654435761u;
        for (int i = 0; i < 6; i++, v >>= 5)
            t[len - 6 + i] = "abcdefghijklmnopqrstuvwxyz012345"[v & 31];
        int fd = open(t, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST) return fd;
    }
    return -1;
}

/* Raw (non-FITS) images need cfitsio; not built here, so augment-xylist reports an error for them. */
int image2xy_files(const char* infn, const char* outfn, anbool do_u8, int downsample,
                   int downsample_as_required, int extension, int plane, simplexy_t* params) { return -1; }
