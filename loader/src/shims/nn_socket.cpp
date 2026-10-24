// nn::socket stand-ins on host BSD sockets.
//
// The engine (through peer_wkc, which passes values straight through) uses the Switch's
// FreeBSD-derived ABI: sockaddr has a length byte, and constants (families, socket options,
// message flags, ioctls) use BSD numbering. Errno values are the exception: Linux numbering.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../host_symbols.h"
#include "../log.h"
#include "dns.h"

namespace {

// --- BSD (nn::socket) constants ---

constexpr int kAfUnspec = 0, kAfInet = 2, kAfInet6 = 28;
constexpr int kSockNonBlock = 0x20000000, kSockCloExec = 0x10000000;
constexpr int kSolSocket = 0xffff;
constexpr int kONonBlock = 0x4;  // BSD O_NONBLOCK for Fcntl(F_SETFL)
constexpr uint32_t kFionbio = 0x8004667e, kFionread = 0x4004667f;

thread_local int t_last_error = 0;  // nn::socket::GetLastError() value (BSD errno)
thread_local int t_h_errno = 0;

// nn::socket::Errno uses Linux numbering (verified: the engine waits for EINPROGRESS == 115 after a
// non-blocking connect), even though socket levels/options/flags use BSD values.
int nn_errno(int e) { return e; }

// Records errno for GetLastError when a call fails.
template <typename T>
T check(T rc) {
    if (rc < 0) t_last_error = nn_errno(errno);
    else t_last_error = 0;
    return rc;
}

int host_family(int f) { return f == kAfInet6 ? AF_INET6 : f == kAfInet ? AF_INET : f == kAfUnspec ? AF_UNSPEC : f; }
int bsd_family(int f) { return f == AF_INET6 ? kAfInet6 : f == AF_INET ? kAfInet : f == AF_UNSPEC ? kAfUnspec : f; }

// sockaddr: BSD { u8 len; u8 family; ... } vs Linux { u16 family; ... }; the rest is identical.
socklen_t to_host_addr(const void* bsd, socklen_t len, sockaddr_storage* out) {
    std::memset(out, 0, sizeof *out);
    if (!bsd || len < 2) return 0;
    std::memcpy(out, bsd, std::min<size_t>(len, sizeof *out));
    out->ss_family = static_cast<sa_family_t>(host_family(static_cast<const uint8_t*>(bsd)[1]));
    return len;
}

void to_bsd_addr(const sockaddr_storage& host, socklen_t host_len, void* bsd, uint32_t* bsd_len) {
    if (!bsd || !bsd_len) return;
    sockaddr_storage tmp = host;
    auto* b = reinterpret_cast<uint8_t*>(&tmp);
    int fam = bsd_family(host.ss_family);
    b[0] = static_cast<uint8_t>(host_len);
    b[1] = static_cast<uint8_t>(fam);
    std::memcpy(bsd, &tmp, std::min<size_t>(*bsd_len, host_len));
    *bsd_len = host_len;
}

bool host_sockopt(int level, int opt, int* hl, int* ho) {
    if (level == kSolSocket) {
        *hl = SOL_SOCKET;
        switch (opt) {
            case 0x0004: *ho = SO_REUSEADDR; return true;
            case 0x0008: *ho = SO_KEEPALIVE; return true;
            case 0x0020: *ho = SO_BROADCAST; return true;
            case 0x0080: *ho = SO_LINGER; return true;
            case 0x0100: *ho = SO_OOBINLINE; return true;
            case 0x0200: *ho = SO_REUSEPORT; return true;
            case 0x1001: *ho = SO_SNDBUF; return true;
            case 0x1002: *ho = SO_RCVBUF; return true;
            case 0x1003: *ho = SO_SNDLOWAT; return true;
            case 0x1004: *ho = SO_RCVLOWAT; return true;
            case 0x1005: *ho = SO_SNDTIMEO; return true;
            case 0x1006: *ho = SO_RCVTIMEO; return true;
            case 0x1007: *ho = SO_ERROR; return true;
            case 0x1008: *ho = SO_TYPE; return true;
            default: return false;
        }
    }
    *hl = level;  // IPPROTO_* numbers match
    if (level == IPPROTO_TCP) {
        switch (opt) {
            case 0x01: *ho = TCP_NODELAY; return true;
            case 0x02: *ho = TCP_MAXSEG; return true;
            case 0x100: *ho = TCP_KEEPIDLE; return true;
            case 0x200: *ho = TCP_KEEPINTVL; return true;
            case 0x400: *ho = TCP_KEEPCNT; return true;
            default: return false;
        }
    }
    *ho = opt;
    return true;
}

int host_msg_flags(int f) {
    int h = MSG_NOSIGNAL;  // never raise SIGPIPE in the host process
    if (f & 0x1) h |= MSG_OOB;
    if (f & 0x2) h |= MSG_PEEK;
    if (f & 0x4) h |= MSG_DONTROUTE;
    if (f & 0x8) h |= MSG_EOR;
    if (f & 0x40) h |= MSG_WAITALL;
    if (f & 0x80) h |= MSG_DONTWAIT;
    return h;
}

// poll: only WRNORM (BSD 0x4 = POLLOUT) and WRBAND (BSD 0x100) differ from Linux.
short host_poll_events(short e) {
    short h = e & ~0x100;
    if (e & 0x100) h |= POLLWRBAND;
    return h;
}
short bsd_poll_events(short h) {
    short e = h & ~(POLLWRNORM | POLLWRBAND);
    if (h & POLLWRNORM) e |= POLLOUT;
    if (h & POLLWRBAND) e |= 0x100;
    return e;
}

// --- socket calls ---

int Socket(int family, int type, int protocol) {
    int htype = type & ~(kSockNonBlock | kSockCloExec);
    if (type & kSockNonBlock) htype |= SOCK_NONBLOCK;
    htype |= SOCK_CLOEXEC;
    int fd = check(::socket(host_family(family), htype, protocol));
    TRACE("socket(%d,%#x,%d) = %d", family, type, protocol, fd);
    return fd;
}

int Close(int fd) { return check(::close(fd)); }
int Listen(int fd, int backlog) { return check(::listen(fd, backlog)); }
int Shutdown(int fd, int how) { return check(::shutdown(fd, how)); }

int Bind(int fd, const void* addr, uint32_t len) {
    sockaddr_storage h;
    socklen_t hl = to_host_addr(addr, len, &h);
    return check(::bind(fd, reinterpret_cast<sockaddr*>(&h), hl));
}

int Connect(int fd, const void* addr, uint32_t len) {
    sockaddr_storage h;
    socklen_t hl = to_host_addr(addr, len, &h);
    int rc = check(::connect(fd, reinterpret_cast<sockaddr*>(&h), hl));
    TRACE("connect(%d, family %d, len %u) = %d (errno %d)", fd, h.ss_family, len, rc, t_last_error);
    return rc;
}

int Accept(int fd, void* addr, uint32_t* len) {
    sockaddr_storage h;
    socklen_t hl = sizeof h;
    int rc = check(::accept4(fd, reinterpret_cast<sockaddr*>(&h), &hl, SOCK_CLOEXEC));
    if (rc >= 0) to_bsd_addr(h, hl, addr, len);
    return rc;
}

using NameFn = int (*)(int, sockaddr*, socklen_t*);
int get_name(NameFn fn, int fd, void* addr, uint32_t* len) {
    sockaddr_storage h;
    socklen_t hl = sizeof h;
    int rc = check(fn(fd, reinterpret_cast<sockaddr*>(&h), &hl));
    if (rc == 0) to_bsd_addr(h, hl, addr, len);
    return rc;
}
int GetSockName(int fd, void* addr, uint32_t* len) { return get_name(::getsockname, fd, addr, len); }
int GetPeerName(int fd, void* addr, uint32_t* len) { return get_name(::getpeername, fd, addr, len); }

ssize_t Send(int fd, const void* buf, size_t n, int flags) {
    ssize_t rc = check(::send(fd, buf, n, host_msg_flags(flags)));
    TRACE("send(%d, %zu, flags %#x) = %zd (errno %d)", fd, n, flags, rc, t_last_error);
    if (trace_enabled()) {
        const auto* b = static_cast<const uint8_t*>(buf);
        char hex[3 * 48 + 1] = {};
        for (size_t i = 0; i < n && i < 48; i++) std::snprintf(hex + 3 * i, 4, "%02x ", b[i]);
        TRACE("  send bytes: %s", hex);
    }
    return rc;
}
ssize_t Recv(int fd, void* buf, size_t n, int flags) {
    ssize_t rc = check(::recv(fd, buf, n, host_msg_flags(flags) & ~MSG_NOSIGNAL));
    TRACE("recv(%d, %zu, flags %#x) = %zd (errno %d)", fd, n, flags, rc, t_last_error);
    return rc;
}

ssize_t SendTo(int fd, const void* buf, size_t n, int flags, const void* addr, uint32_t len) {
    sockaddr_storage h;
    socklen_t hl = to_host_addr(addr, len, &h);
    return check(::sendto(fd, buf, n, host_msg_flags(flags), addr ? reinterpret_cast<sockaddr*>(&h) : nullptr, hl));
}

ssize_t RecvFrom(int fd, void* buf, size_t n, int flags, void* addr, uint32_t* len) {
    sockaddr_storage h;
    socklen_t hl = sizeof h;
    ssize_t rc = check(::recvfrom(fd, buf, n, host_msg_flags(flags) & ~MSG_NOSIGNAL, reinterpret_cast<sockaddr*>(&h), &hl));
    if (rc >= 0) to_bsd_addr(h, hl, addr, len);
    return rc;
}

int SetSockOpt(int fd, int level, int opt, const void* val, uint32_t len) {
    int hl, ho;
    if (!host_sockopt(level, opt, &hl, &ho)) {
        TRACE("setsockopt: ignoring unknown option level=%#x opt=%#x", level, opt);
        return 0;
    }
    return check(::setsockopt(fd, hl, ho, val, len));
}

int GetSockOpt(int fd, int level, int opt, void* val, uint32_t* len) {
    int hl, ho;
    if (!host_sockopt(level, opt, &hl, &ho)) {
        t_last_error = 42;  // ENOPROTOOPT
        return -1;
    }
    socklen_t l = *len;
    int rc = check(::getsockopt(fd, hl, ho, val, &l));
    *len = l;
    TRACE("getsockopt(%d, %#x, %#x) = %d (value %d)", fd, level, opt, rc, l >= sizeof(int) ? *static_cast<int*>(val) : -1);
    if (rc == 0 && hl == SOL_SOCKET && ho == SO_ERROR && l >= sizeof(int)) {
        int* e = static_cast<int*>(val);
        *e = nn_errno(*e);  // SO_ERROR
    }
    return rc;
}

int Fcntl(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    int arg = va_arg(ap, int);
    va_end(ap);
    if (cmd == F_GETFL) {
        int fl = check(::fcntl(fd, F_GETFL));
        if (fl < 0) return fl;
        return (fl & ~O_NONBLOCK) | ((fl & O_NONBLOCK) ? kONonBlock : 0);
    }
    if (cmd == F_SETFL) {
        int fl = (arg & ~kONonBlock) | ((arg & kONonBlock) ? O_NONBLOCK : 0);
        return check(::fcntl(fd, F_SETFL, fl));
    }
    return check(::fcntl(fd, cmd, arg));
}

int Ioctl(int fd, uint32_t cmd, void* data, size_t) {
    if (cmd == kFionbio) return check(::ioctl(fd, FIONBIO, data));
    if (cmd == kFionread) return check(::ioctl(fd, FIONREAD, data));
    LOG("socket Ioctl: unsupported command %#x", cmd);
    t_last_error = 22;  // EINVAL
    return -1;
}

struct PollFd {
    int fd;
    short events;
    short revents;
};

int Poll(PollFd* fds, unsigned long n, int timeout) {
    // pollfd layout is identical; translate flag bits in place.
    for (unsigned long i = 0; i < n; i++) fds[i].events = host_poll_events(fds[i].events);
    int rc = check(::poll(reinterpret_cast<pollfd*>(fds), n, timeout));
    for (unsigned long i = 0; i < n; i++) {
        fds[i].events = bsd_poll_events(fds[i].events);
        fds[i].revents = bsd_poll_events(fds[i].revents);
    }
    TRACE("poll(n=%lu, timeout=%d) = %d (fd0=%d revents=%#x)", n, timeout, rc, n ? fds[0].fd : -1, n ? fds[0].revents : 0);
    return rc;
}

// FdSet is 1024 bits in both ABIs, with the same bit order on little-endian.
int Select(int n, fd_set* r, fd_set* w, fd_set* e, timeval* tv) {
    TRACE("select(n=%d, r=%d w=%d e=%d, tv=%ld.%06ld)", n, r != nullptr, w != nullptr, e != nullptr,
          tv ? (long)tv->tv_sec : -1L, tv ? (long)tv->tv_usec : 0L);
    int rc = check(::select(n, r, w, e, tv));
    TRACE("select -> %d", rc);
    return rc;
}

// --- resolver ---

// HostEnt matches struct hostent's layout. Lookups use the built-in resolver (dns.cpp), not glibc's
// NSS-based gethostbyname, so the static binary doesn't need NSS modules at runtime.
struct HostEntStorage {
    hostent ent{};
    std::string name;
    std::vector<in_addr> addrs;
    std::vector<char*> addr_ptrs;
    char* aliases[1] = {nullptr};
};
thread_local HostEntStorage t_hostent;

hostent* GetHostEntByName(const char* name, const void*, size_t) {
    int err = 0;
    auto addrs = nxb::resolve_ipv4(name ? name : "", &err);
    TRACE("gethostbyname(%s) -> %zu", name ? name : "(null)", addrs.size());
    if (addrs.empty()) {
        t_h_errno = err ? err : HOST_NOT_FOUND;
        return nullptr;
    }
    HostEntStorage& h = t_hostent;
    h.name = name;
    h.addrs = std::move(addrs);
    h.addr_ptrs.clear();
    for (auto& a : h.addrs) h.addr_ptrs.push_back(reinterpret_cast<char*>(&a));
    h.addr_ptrs.push_back(nullptr);
    h.ent.h_name = &h.name[0];
    h.ent.h_aliases = h.aliases;
    h.ent.h_addrtype = kAfInet;
    h.ent.h_length = 4;
    h.ent.h_addr_list = h.addr_ptrs.data();
    return &h.ent;
}

hostent* GetHostEntByAddr(const void*, uint32_t, int) {
    t_h_errno = HOST_NOT_FOUND;  // reverse lookups aren't supported
    return nullptr;
}

int* GetHError() { return &t_h_errno; }
int GetLastError() { return t_last_error; }

// Nothing in the engine calls this (wkcNetGetAddrInfo is exported but unused), and the BSD vs
// Linux addrinfo field order hasn't been confirmed, so fail cleanly rather than guess.
int GetAddrInfo(const char* node, const char*, const void*, void** out, const void*, size_t) {
    LOG("socket GetAddrInfo(%s): not implemented", node ? node : "(null)");
    if (out) *out = nullptr;
    return 4;  // EAI_FAIL
}
void FreeAddrInfo(void*) {}

int InetAton(const char* s, in_addr* out) { return ::inet_aton(s, out); }
const char* InetNtoa(in_addr a) { return ::inet_ntoa(a); }
uint32_t InetHtonl(uint32_t v) { return htonl(v); }
uint16_t InetHtons(uint16_t v) { return htons(v); }
uint32_t InetNtohl(uint32_t v) { return ntohl(v); }
uint16_t InetNtohs(uint16_t v) { return ntohs(v); }

HostSymbols symbols({
    {"_ZN2nn6socket6SocketENS0_6FamilyENS0_4TypeENS0_8ProtocolE", (void*)&Socket},
    {"_ZN2nn6socket5CloseEi", (void*)&Close},
    {"_ZN2nn6socket6ListenEii", (void*)&Listen},
    {"_ZN2nn6socket8ShutdownEiNS0_14ShutdownMethodE", (void*)&Shutdown},
    {"_ZN2nn6socket4BindEiPKNS0_8SockAddrEj", (void*)&Bind},
    {"_ZN2nn6socket7ConnectEiPKNS0_8SockAddrEj", (void*)&Connect},
    {"_ZN2nn6socket6AcceptEiPNS0_8SockAddrEPj", (void*)&Accept},
    {"_ZN2nn6socket11GetSockNameEiPNS0_8SockAddrEPj", (void*)&GetSockName},
    {"_ZN2nn6socket11GetPeerNameEiPNS0_8SockAddrEPj", (void*)&GetPeerName},
    {"_ZN2nn6socket4SendEiPKvmNS0_7MsgFlagE", (void*)&Send},
    {"_ZN2nn6socket4RecvEiPvmNS0_7MsgFlagE", (void*)&Recv},
    {"_ZN2nn6socket6SendToEiPKvmNS0_7MsgFlagEPKNS0_8SockAddrEj", (void*)&SendTo},
    {"_ZN2nn6socket8RecvFromEiPvmNS0_7MsgFlagEPNS0_8SockAddrEPj", (void*)&RecvFrom},
    {"_ZN2nn6socket10SetSockOptEiNS0_5LevelENS0_6OptionEPKvj", (void*)&SetSockOpt},
    {"_ZN2nn6socket10GetSockOptEiNS0_5LevelENS0_6OptionEPvPj", (void*)&GetSockOpt},
    {"_ZN2nn6socket5FcntlEiNS0_12FcntlCommandEz", (void*)&Fcntl},
    {"_ZN2nn6socket5IoctlEiNS0_12IoctlCommandEPvm", (void*)&Ioctl},
    {"_ZN2nn6socket4PollEPNS0_6PollFdEmi", (void*)&Poll},
    {"_ZN2nn6socket6SelectEiPNS0_5FdSetES2_S2_PNS0_7TimeValE", (void*)&Select},
    {"_ZN2nn6socket16GetHostEntByNameEPKcPKNS0_14ResolverOptionEm", (void*)&GetHostEntByName},
    {"_ZN2nn6socket16GetHostEntByAddrEPKvjNS0_6FamilyE", (void*)&GetHostEntByAddr},
    {"_ZN2nn6socket9GetHErrorEv", (void*)&GetHError},
    {"_ZN2nn6socket12GetLastErrorEv", (void*)&GetLastError},
    {"_ZN2nn6socket11GetAddrInfoEPKcS2_PKNS0_8AddrInfoEPPS3_PKNS0_14ResolverOptionEm", (void*)&GetAddrInfo},
    {"_ZN2nn6socket12FreeAddrInfoEPNS0_8AddrInfoE", (void*)&FreeAddrInfo},
    {"_ZN2nn6socket8InetAtonEPKcPNS0_6InAddrE", (void*)&InetAton},
    {"_ZN2nn6socket8InetNtoaENS0_6InAddrE", (void*)&InetNtoa},
    {"_ZN2nn6socket9InetHtonlEj", (void*)&InetHtonl},
    {"_ZN2nn6socket9InetHtonsEt", (void*)&InetHtons},
    {"_ZN2nn6socket9InetNtohlEj", (void*)&InetNtohl},
    {"_ZN2nn6socket9InetNtohsEt", (void*)&InetNtohs},
});

}  // namespace
