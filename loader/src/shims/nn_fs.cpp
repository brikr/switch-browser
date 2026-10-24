// nn::fs stand-ins: basic file I/O on host files, with "mount:/" paths mapped by fs_paths.

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>

#include "../fs_paths.h"
#include "../host_symbols.h"
#include "../log.h"

namespace {

using Result = uint32_t;

// nn::fs results: module 2, description in bits 9+.
constexpr Result MakeResult(uint32_t desc) { return 2 | (desc << 9); }
constexpr Result kSuccess = 0;
constexpr Result kPathNotFound = MakeResult(1);
constexpr Result kPathAlreadyExists = MakeResult(2);
constexpr Result kTargetLocked = MakeResult(7);
constexpr Result kUnexpected = MakeResult(5000);

enum OpenMode { kRead = 1, kWrite = 2, kAllowAppend = 4 };
enum WriteOptionFlag { kFlush = 1 };

struct FileHandle { void* handle; };  // passed by value in one register
struct WriteOption { int flags; };

int fd_of(FileHandle h) { return static_cast<int>(reinterpret_cast<intptr_t>(h.handle)); }

Result from_errno() {
    switch (errno) {
        case ENOENT: case ENOTDIR: return kPathNotFound;
        case EEXIST: return kPathAlreadyExists;
        case EBUSY: return kTargetLocked;
        default: return kUnexpected;
    }
}

bool host_path(const char* path, char (&buf)[PATH_MAX]) { return nxb_translate_path(path, buf, sizeof buf) != nullptr; }

Result OpenFile(FileHandle* out, const char* path, int mode) {
    char p[PATH_MAX];
    if (!host_path(path, p)) return kPathNotFound;
    int flags = (mode & kWrite) ? ((mode & kRead) ? O_RDWR : O_WRONLY) : O_RDONLY;
    int fd = ::open(p, flags | O_CLOEXEC);
    TRACE("fs::OpenFile %s mode=%d -> %d", path, mode, fd);
    if (fd < 0) return from_errno();
    out->handle = reinterpret_cast<void*>(static_cast<intptr_t>(fd));
    return kSuccess;
}

void CloseFile(FileHandle h) { ::close(fd_of(h)); }

Result ReadFile(size_t* out_read, FileHandle h, int64_t offset, void* buf, size_t size) {
    ssize_t n = ::pread(fd_of(h), buf, size, offset);
    if (n < 0) return from_errno();
    *out_read = static_cast<size_t>(n);
    return kSuccess;
}

Result WriteFile(FileHandle h, int64_t offset, const void* buf, size_t size, const WriteOption& opt) {
    ssize_t n = ::pwrite(fd_of(h), buf, size, offset);
    if (n < 0 || static_cast<size_t>(n) != size) return from_errno();
    if (opt.flags & kFlush) ::fsync(fd_of(h));
    return kSuccess;
}

Result FlushFile(FileHandle h) { return ::fsync(fd_of(h)) == 0 ? kSuccess : from_errno(); }

Result GetFileSize(int64_t* out, FileHandle h) {
    struct stat st;
    if (::fstat(fd_of(h), &st) != 0) return from_errno();
    *out = st.st_size;
    return kSuccess;
}

Result CreateFile(const char* path, int64_t size) {
    char p[PATH_MAX];
    if (!host_path(path, p)) return kPathNotFound;
    int fd = ::open(p, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0644);
    TRACE("fs::CreateFile %s size=%lld -> %d", path, (long long)size, fd);
    if (fd < 0) return from_errno();
    Result r = ::ftruncate(fd, size) == 0 ? kSuccess : from_errno();
    ::close(fd);
    return r;
}

Result CreateDirectory(const char* path) {
    char p[PATH_MAX];
    if (!host_path(path, p)) return kPathNotFound;
    return ::mkdir(p, 0755) == 0 ? kSuccess : from_errno();
}

HostSymbols symbols({
    {"_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci", (void*)&OpenFile},
    {"_ZN2nn2fs9CloseFileENS0_10FileHandleE", (void*)&CloseFile},
    {"_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm", (void*)&ReadFile},
    {"_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE", (void*)&WriteFile},
    {"_ZN2nn2fs9FlushFileENS0_10FileHandleE", (void*)&FlushFile},
    {"_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE", (void*)&GetFileSize},
    {"_ZN2nn2fs10CreateFileEPKcl", (void*)&CreateFile},
    {"_ZN2nn2fs15CreateDirectoryEPKc", (void*)&CreateDirectory},
});

}  // namespace
