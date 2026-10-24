#include "fs_paths.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

#include "log.h"

namespace {

std::string env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return v && *v ? v : fallback;
}

void mkdirs(const std::string& dir) {
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == '/') ::mkdir(dir.substr(0, i).c_str(), 0755);
}

}  // namespace

extern "C" const char* nxb_translate_path(const char* path, char* buf, size_t size) {
    const char* colon = std::strstr(path, ":/");
    const char* slash = std::strchr(path, '/');
    std::string out;
    if (!colon || (slash && slash < colon)) {
        out = path;
    } else {
        std::string mount(path, colon - path);
        std::string rest(colon + 1);  // keeps the leading '/'
        if (mount == "rom") {
            out = env_or("NXB_ROMFS", "romfs") + rest;
        } else {
            std::string root = env_or("NXB_DATA", "nxb-data") + "/" + mount;
            mkdirs(root);
            out = root + rest;
        }
    }
    TRACE("path %s -> %s", path, out.c_str());
    if (out.size() + 1 > size) return nullptr;
    std::memcpy(buf, out.c_str(), out.size() + 1);
    return buf;
}
