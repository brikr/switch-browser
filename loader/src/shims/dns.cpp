#include "dns.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>

#include "../log.h"

namespace nxb {
namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<in_addr> from_hosts_file(const std::string& name) {
    std::vector<in_addr> out;
    std::ifstream in("/etc/hosts");
    std::string line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find('#'));
        std::istringstream ss(line);
        std::string ip, host;
        if (!(ss >> ip)) continue;
        in_addr a;
        if (inet_pton(AF_INET, ip.c_str(), &a) != 1) continue;
        while (ss >> host)
            if (lower(host) == name) out.push_back(a);
    }
    return out;
}

std::string nameserver() {
    if (const char* e = std::getenv("NXB_DNS"); e && *e) return e;
    std::ifstream in("/etc/resolv.conf");
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string key, ip;
        in_addr a;
        if (ss >> key >> ip && key == "nameserver" && inet_pton(AF_INET, ip.c_str(), &a) == 1) return ip;
    }
    return "8.8.8.8";
}

// Skips an encoded (possibly compressed) name; returns the offset after it, or 0 on error.
size_t skip_name(const uint8_t* p, size_t len, size_t off) {
    while (off < len) {
        uint8_t l = p[off];
        if (l == 0) return off + 1;
        if ((l & 0xC0) == 0xC0) return off + 2 <= len ? off + 2 : 0;
        off += 1 + l;
    }
    return 0;
}

std::vector<in_addr> query(const std::string& server, const std::string& name, int* h_err) {
    std::vector<in_addr> out;
    std::vector<uint8_t> q;
    uint16_t id = static_cast<uint16_t>(std::random_device{}());
    q.insert(q.end(), {uint8_t(id >> 8), uint8_t(id), 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0});  // RD, 1 question
    std::istringstream labels(name);
    std::string label;
    while (std::getline(labels, label, '.')) {
        if (label.empty() || label.size() > 63) {
            *h_err = HOST_NOT_FOUND;
            return out;
        }
        q.push_back(static_cast<uint8_t>(label.size()));
        q.insert(q.end(), label.begin(), label.end());
    }
    q.insert(q.end(), {0, 0, 1, 0, 1});  // root, QTYPE A, QCLASS IN

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(53);
    inet_pton(AF_INET, server.c_str(), &sa.sin_addr);
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        *h_err = TRY_AGAIN;
        return out;
    }
    uint8_t buf[1500];
    ssize_t n = -1;
    for (int attempt = 0; attempt < 3 && n < 0; attempt++) {
        sendto(fd, q.data(), q.size(), 0, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
        pollfd pfd{fd, POLLIN, 0};
        if (poll(&pfd, 1, 2000) == 1) {
            n = recv(fd, buf, sizeof buf, 0);
            if (n >= 12 && (buf[0] != q[0] || buf[1] != q[1])) n = -1;  // not our reply
        }
    }
    close(fd);
    if (n < 12) {
        *h_err = TRY_AGAIN;
        return out;
    }
    int rcode = buf[3] & 0x0F;
    uint16_t qd = (buf[4] << 8) | buf[5], an = (buf[6] << 8) | buf[7];
    size_t off = 12;
    for (int i = 0; i < qd && off; i++) off = skip_name(buf, n, off) + (off ? 4 : 0);
    for (int i = 0; i < an && off && off + 10 <= static_cast<size_t>(n); i++) {
        off = skip_name(buf, n, off);
        if (!off || off + 10 > static_cast<size_t>(n)) break;
        uint16_t type = (buf[off] << 8) | buf[off + 1];
        uint16_t rdlen = (buf[off + 8] << 8) | buf[off + 9];
        off += 10;
        if (off + rdlen > static_cast<size_t>(n)) break;
        if (type == 1 && rdlen == 4) {  // A (CNAMEs are followed by the server; their A records come along)
            in_addr a;
            std::memcpy(&a, buf + off, 4);
            out.push_back(a);
        }
        off += rdlen;
    }
    if (out.empty()) *h_err = rcode == 3 ? HOST_NOT_FOUND : NO_DATA;
    return out;
}

}  // namespace

std::vector<in_addr> resolve_ipv4(const std::string& raw, int* h_err) {
    *h_err = 0;
    std::string name = lower(raw);
    if (!name.empty() && name.back() == '.') name.pop_back();
    in_addr a;
    if (inet_pton(AF_INET, name.c_str(), &a) == 1) return {a};
    if (name == "localhost") {
        inet_pton(AF_INET, "127.0.0.1", &a);
        return {a};
    }
    auto hosts = from_hosts_file(name);
    if (!hosts.empty()) return hosts;
    auto result = query(nameserver(), name, h_err);
    TRACE("dns %s -> %zu address(es)", name.c_str(), result.size());
    return result;
}

}  // namespace nxb
