/* Optional "host mmap" support, linked with -Wl,--wrap=fopen,--wrap=fclose,--wrap=mmap,--wrap=munmap.
 *
 * wasi-libc's emulated mmap() (-lwasi-emulated-mman) reads the whole file into memory.  A host that can
 * map file views lazily (e.g. a Java MemoryCreator) exports  an.map / an.unmap  and this file asks it to
 * back large read-only file mappings instead.  Anything else falls through to wasi-libc's mmap().
 *
 * The host sees paths, not descriptors, so fopen()/fclose() are wrapped to remember each fd's path. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

__attribute__((import_module("an"), import_name("map")))
extern int an_host_map(void* guest, unsigned long len, const char* path, long long off);
__attribute__((import_module("an"), import_name("unmap")))
extern int an_host_unmap(void* guest, unsigned long len);

#define AN_HP 65536u                       /* wasm page size: host mappings must be page aligned */
#ifndef AN_HOST_MAP_MIN
#define AN_HOST_MAP_MIN (1u << 20)         /* small mappings: a plain read is cheaper than a host call */
#endif
#define AN_NFD 256

static char* fd_path[AN_NFD];

FILE* __real_fopen(const char* path, const char* mode);
FILE* __wrap_fopen(const char* path, const char* mode) {
    FILE* f = __real_fopen(path, mode);
    if (f) {
        int fd = fileno(f);
        if (fd >= 0 && fd < AN_NFD) { free(fd_path[fd]); fd_path[fd] = strdup(path); }
    }
    return f;
}

int __real_fclose(FILE* f);
int __wrap_fclose(FILE* f) {
    int fd = f ? fileno(f) : -1;
    if (fd >= 0 && fd < AN_NFD) { free(fd_path[fd]); fd_path[fd] = NULL; }
    return __real_fclose(f);
}

struct host_map { char* raw; char* al; size_t alen; struct host_map* next; };
static struct host_map* host_maps;

static void* host_mmap(size_t len, int fd, off_t off) {
    size_t alen; char *raw, *al; struct host_map* m;
    struct stat a, b;
    if (len < AN_HOST_MAP_MIN || fd < 0 || fd >= AN_NFD || !fd_path[fd] || (off % AN_HP)) return NULL;
    /* the fd must still be the file we recorded (fds get reused by opens we do not see) */
    if (fstat(fd, &a) || stat(fd_path[fd], &b) || a.st_size != b.st_size ||
        (a.st_ino && b.st_ino && a.st_ino != b.st_ino)) return NULL;   /* ino can be 0 on some hosts */
    alen = (len + AN_HP - 1) & ~(size_t)(AN_HP - 1);
    raw = malloc(alen + AN_HP); if (!raw) return NULL;
    al = (char*)(((size_t)raw + AN_HP - 1) & ~(size_t)(AN_HP - 1));
    if (an_host_map(al, alen, fd_path[fd], (long long)off) != 0) { free(raw); return NULL; }
    m = malloc(sizeof *m);
    if (!m) { an_host_unmap(al, alen); free(raw); return NULL; }
    m->raw = raw; m->al = al; m->alen = alen; m->next = host_maps; host_maps = m;
    return al;
}

static int host_munmap(void* addr) {
    struct host_map **pp = &host_maps, *m;
    for (; (m = *pp); pp = &m->next) if ((void*)m->al == addr) {
        *pp = m->next;
        an_host_unmap(m->al, m->alen);     /* host puts back ordinary zeroed memory first, then malloc may reuse it */
        free(m->raw); free(m);
        return 1;
    }
    return 0;
}

void* __real_mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off);
void* __wrap_mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off) {
    if (!(flags & MAP_ANONYMOUS) && !(flags & MAP_FIXED)) {
        void* p = host_mmap(len, fd, off);
        if (p) return p;
    }
    return __real_mmap(addr, len, prot, flags, fd, off);
}

int __real_munmap(void* addr, size_t len);
int __wrap_munmap(void* addr, size_t len) {
    if (host_munmap(addr)) return 0;
    return __real_munmap(addr, len);
}
