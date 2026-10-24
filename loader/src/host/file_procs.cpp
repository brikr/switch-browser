// File-system callbacks (WKCWebKitSetFileSystemProcs). The file peer forwards every operation to
// these; without them the first file access (e.g. after a network load) calls a null pointer.
// Implemented on host stdio/POSIX, with Horizon "mount:/" paths translated.
//
// Table layouts (recovered from the peers' wkcFile*Peer code; the 0.30.0 header is not in the OSS
// package): core_3 = 33 procs as in 3.30.3's wkcfileprocs.h. core_0 = 32 procs: no Fileno or
// DeleteDirectory, and PathByAppendingComponent between PathGetFileName and HomeDirectoryPath.

#include "host/file_procs.h"

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <wkc/wkcfileprocs.h>  // typedefs (3.30.3 header; signatures are the same in 0.30.0)

#include "cfi_gate.h"
#include "fs_paths.h"
#include "log.h"

namespace nxb {
namespace {

std::string data_root() {
    const char* d = std::getenv("NXB_DATA");
    return d && *d ? d : "nxb-data";
}

// "mount:/x" paths go through the mount table; relative paths (the engine uses e.g. "cache/...")
// are rooted in the data directory.
std::string host(const char* path) {
    char buf[PATH_MAX];
    std::string in = path ? path : "";
    if (!in.empty() && in[0] != '/' && in.find(":/") == std::string::npos) return data_root() + "/" + in;
    return nxb_translate_path(in.c_str(), buf, sizeof buf) ? buf : "";
}

void make_parent_dirs(const std::string& file) {
    for (size_t i = 1; i < file.size(); i++)
        if (file[i] == '/') mkdir(file.substr(0, i).c_str(), 0755);
}

FILE* F(void* fd) { return static_cast<FILE*>(fd); }

int copy_out(const std::string& s, char* out, int max) {
    if (!out || max <= 0) return 0;
    size_t n = std::min<size_t>(s.size(), static_cast<size_t>(max) - 1);
    std::memcpy(out, s.data(), n);
    out[n] = '\0';
    return static_cast<int>(n);
}

void* f_open(int, const char* name, const char* mode) {
    std::string path = host(name);
    if (mode && (std::strchr(mode, 'w') || std::strchr(mode, 'a'))) make_parent_dirs(path);
    FILE* f = std::fopen(path.c_str(), mode);
    TRACE("file fopen(%s, %s) -> %p", name, mode, (void*)f);
    return f;
}
int f_close(void* fd) { return std::fclose(F(fd)); }
wkc_uint64 f_read(void* buf, wkc_uint64 size, wkc_uint64 count, void* fd) { return std::fread(buf, size, count, F(fd)); }
wkc_uint64 f_write(const void* buf, wkc_uint64 size, wkc_uint64 count, void* fd) {
    return std::fwrite(buf, size, count, F(fd));
}
int f_eof(void* fd) { return std::feof(F(fd)); }
int f_stat(void* fd, struct stat* st) { return fstat(fileno(F(fd)), st); }
int f_seek(void* fd, wkc_int64 off, int whence) { return fseeko(F(fd), off, whence); }
wkc_int64 f_tell(void* fd) { return ftello(F(fd)); }
int f_truncate(void* fd, wkc_uint64 len) { return ftruncate(fileno(F(fd)), static_cast<off_t>(len)); }
int f_lock(void*, int, wkc_uint64, wkc_uint64) { return 0; }  // single process: nothing to lock against
int f_unlock(void*, int, wkc_uint64, wkc_uint64) { return 0; }
int f_sync(int fd) { return fsync(fd); }
int f_getc(void* fd) { return std::fgetc(F(fd)); }
int f_putc(int c, void* fd) { return std::fputc(c, F(fd)); }
char* f_gets(char* s, int size, void* fd) { return std::fgets(s, size, F(fd)); }
int f_puts(const char* s, void* fd) { return std::fputs(s, F(fd)); }
int f_flush(void* fd) { return std::fflush(F(fd)); }
int f_error(void* fd) { return std::ferror(F(fd)); }
int f_access(const char* name, int mode) { return access(host(name).c_str(), mode); }
int f_unlink(const char* path) { return unlink(host(path).c_str()); }
int f_statpath(const char* name, struct stat* st) { return stat(host(name).c_str(), st); }
int f_errno() { return errno; }
[[maybe_unused]] int f_fileno(void* fd) { return fileno(F(fd)); }

bool f_mkdirs(const char* path) {
    std::string p = host(path);
    for (size_t i = 1; i <= p.size(); i++)
        if (i == p.size() || p[i] == '/') {
            std::string part = p.substr(0, i);
            if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
    return true;
}

int f_filename(const char* path, char* out, int max) {
    std::string p = path ? path : "";
    size_t slash = p.find_last_of('/');
    return copy_out(slash == std::string::npos ? p : p.substr(slash + 1), out, max);
}

[[maybe_unused]] int f_append(const char* path, const char* component, char* out, int max) {
    std::string p = path ? path : "";
    if (!p.empty() && p.back() != '/') p += '/';
    return copy_out(p + (component ? component : ""), out, max);
}

int f_home(char* out, int max) {
    std::string home = data_root() + "/home";
    mkdir(home.c_str(), 0755);
    return copy_out(home, out, max);
}

int f_dirname(const char* path, char* out, int max) {
    std::string p = path ? path : "";
    size_t slash = p.find_last_of('/');
    return copy_out(slash == std::string::npos ? std::string() : p.substr(0, slash), out, max);
}

bool matches(const char* name, const char* filter) {
    if (!std::strcmp(name, ".") || !std::strcmp(name, "..")) return false;
    return !filter || !*filter || fnmatch(filter, name, 0) == 0;
}

int f_listdir(const char* path, const char* filter, char** out, int max_len, int max_entries) {
    DIR* d = opendir(host(path).c_str());
    if (!d) return 0;
    int n = 0;
    while (dirent* e = readdir(d)) {
        if (!matches(e->d_name, filter)) continue;
        if (out && n < max_entries) copy_out(e->d_name, out[n], max_len);
        n++;
    }
    closedir(d);
    return n;
}

void* f_tmpfile(const char* prefix, char* out, int max) {
    const char* d = std::getenv("NXB_DATA");
    std::string tmpl = std::string(d && *d ? d : "nxb-data") + "/" + (prefix ? prefix : "tmp") + "XXXXXX";
    std::string dir = tmpl.substr(0, tmpl.find_last_of('/'));
    mkdir(dir.c_str(), 0755);
    int fd = mkstemp(&tmpl[0]);
    if (fd < 0) return nullptr;
    copy_out(tmpl, out, max);
    return fdopen(fd, "w+b");
}

struct HostDir {
    DIR* dir;
    std::string filter;
};

void* f_opendir(int, const char* path, const char* filter) {
    DIR* d = opendir(host(path).c_str());
    return d ? new HostDir{d, filter ? filter : ""} : nullptr;
}
void f_closedir(void* d) {
    auto* h = static_cast<HostDir*>(d);
    closedir(h->dir);
    delete h;
}
int f_readdir(void* d, char* out, int max) {
    auto* h = static_cast<HostDir*>(d);
    while (dirent* e = readdir(h->dir))
        if (matches(e->d_name, h->filter.c_str())) return copy_out(e->d_name, out, max);
    return 0;
}

int remove_tree(const std::string& p) {
    DIR* d = opendir(p.c_str());
    if (!d) return std::remove(p.c_str());
    while (dirent* e = readdir(d))
        if (std::strcmp(e->d_name, ".") && std::strcmp(e->d_name, "..")) remove_tree(p + "/" + e->d_name);
    closedir(d);
    return rmdir(p.c_str());
}
[[maybe_unused]] int f_rmdir(const char* dir, bool recursive) {
    std::string p = host(dir);
    return recursive ? remove_tree(p) : rmdir(p.c_str());
}

}  // namespace

void install_file_procs() {
#if NXB_CORE == 3
    static WKCFileProcs procs = {
        gate_function(&f_open),      gate_function(&f_close),    gate_function(&f_read),
        gate_function(&f_write),     gate_function(&f_eof),      gate_function(&f_stat),
        gate_function(&f_seek),      gate_function(&f_tell),     gate_function(&f_truncate),
        gate_function(&f_lock),      gate_function(&f_unlock),   gate_function(&f_sync),
        gate_function(&f_getc),      gate_function(&f_putc),     gate_function(&f_gets),
        gate_function(&f_puts),      gate_function(&f_flush),    gate_function(&f_error),
        gate_function(&f_access),    gate_function(&f_unlink),   gate_function(&f_statpath),
        gate_function(&f_errno),     gate_function(&f_fileno),   gate_function(&f_mkdirs),
        gate_function(&f_filename),  gate_function(&f_home),     gate_function(&f_listdir),
        gate_function(&f_dirname),   gate_function(&f_tmpfile),  gate_function(&f_opendir),
        gate_function(&f_closedir),  gate_function(&f_readdir),  gate_function(&f_rmdir),
    };
    static_assert(sizeof procs == 0x108, "core_3's file peer copies 0x108 bytes");
    WKC::WKCWebKitSetFileSystemProcs(&procs);
#else
    static const void* procs[] = {
        (void*)gate_function(&f_open),     (void*)gate_function(&f_close),    (void*)gate_function(&f_read),
        (void*)gate_function(&f_write),    (void*)gate_function(&f_eof),      (void*)gate_function(&f_stat),
        (void*)gate_function(&f_seek),     (void*)gate_function(&f_tell),     (void*)gate_function(&f_truncate),
        (void*)gate_function(&f_lock),     (void*)gate_function(&f_unlock),   (void*)gate_function(&f_sync),
        (void*)gate_function(&f_getc),     (void*)gate_function(&f_putc),     (void*)gate_function(&f_gets),
        (void*)gate_function(&f_puts),     (void*)gate_function(&f_flush),    (void*)gate_function(&f_error),
        (void*)gate_function(&f_access),   (void*)gate_function(&f_unlink),   (void*)gate_function(&f_statpath),
        (void*)gate_function(&f_errno),    (void*)gate_function(&f_mkdirs),   (void*)gate_function(&f_filename),
        (void*)gate_function(&f_append),   (void*)gate_function(&f_home),     (void*)gate_function(&f_listdir),
        (void*)gate_function(&f_dirname),  (void*)gate_function(&f_tmpfile),  (void*)gate_function(&f_opendir),
        (void*)gate_function(&f_closedir), (void*)gate_function(&f_readdir),
    };
    static_assert(sizeof procs == 0x100, "core_0's file peer copies 0x100 bytes");
    WKC::WKCWebKitSetFileSystemProcs(reinterpret_cast<const WKC::FileSystemProcs*>(procs));
#endif
}

}  // namespace nxb
