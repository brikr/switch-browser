/* libc functions that take paths: translate Horizon "mount:/" paths before calling glibc. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../fs_paths.h"

#define TRANSLATE(path, fail)                                             \
    char buf_[PATH_MAX];                                                  \
    const char* host_ = nxb_translate_path((path), buf_, sizeof buf_);    \
    if (!host_) {                                                         \
        errno = ENAMETOOLONG;                                             \
        return (fail);                                                    \
    }

FILE* nxb_fopen(const char* path, const char* mode) {
    TRANSLATE(path, NULL);
    return fopen(host_, mode);
}

int nxb_open(const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    TRANSLATE(path, -1);
    return open(host_, flags, mode);
}

int nxb_access(const char* path, int mode) {
    TRANSLATE(path, -1);
    return access(host_, mode);
}

int nxb_mkdir(const char* path, mode_t mode) {
    TRANSLATE(path, -1);
    return mkdir(host_, mode);
}

int nxb_rmdir(const char* path) {
    TRANSLATE(path, -1);
    return rmdir(host_);
}

int nxb_remove(const char* path) {
    TRANSLATE(path, -1);
    return remove(host_);
}
