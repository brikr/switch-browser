#include "host/ws_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include "log.h"

namespace nxb {
namespace {

// --- SHA-1 and base64 for the WebSocket handshake ---

std::string sha1(const std::string& msg) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string m = msg;
    uint64_t bits = static_cast<uint64_t>(msg.size()) * 8;
    m += static_cast<char>(0x80);
    while (m.size() % 64 != 56) m += '\0';
    for (int i = 7; i >= 0; i--) m += static_cast<char>(bits >> (i * 8));
    auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (uint8_t(m[off + 4 * i]) << 24) | (uint8_t(m[off + 4 * i + 1]) << 16) |
                   (uint8_t(m[off + 4 * i + 2]) << 8) | uint8_t(m[off + 4 * i + 3]);
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
            else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
            else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
            else f = b ^ c ^ d, k = 0xCA62C1D6;
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d, d = c, c = rol(b, 30), b = a, a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
    }
    std::string out;
    for (uint32_t v : h)
        for (int i = 3; i >= 0; i--) out += static_cast<char>(v >> (i * 8));
    return out;
}

std::string base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8) | uint8_t(in[i + 2]);
        out += t[v >> 18], out += t[(v >> 12) & 63], out += t[(v >> 6) & 63], out += t[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = uint8_t(in[i]) << 16;
        out += t[v >> 18], out += t[(v >> 12) & 63], out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8);
        out += t[v >> 18], out += t[(v >> 12) & 63], out += t[(v >> 6) & 63], out += '=';
    }
    return out;
}

bool write_all(int fd, const void* data, size_t size) {
    auto* p = static_cast<const uint8_t*>(data);
    while (size) {
        ssize_t n = ::send(fd, p, size, MSG_NOSIGNAL);
        if (n <= 0) return false;
        p += n, size -= static_cast<size_t>(n);
    }
    return true;
}

bool read_all(int fd, void* data, size_t size) {
    auto* p = static_cast<uint8_t*>(data);
    while (size) {
        ssize_t n = ::recv(fd, p, size, 0);
        if (n <= 0) return false;
        p += n, size -= static_cast<size_t>(n);
    }
    return true;
}

std::string header_value(const std::string& req, const std::string& name) {
    std::istringstream in(req);
    std::string line;
    while (std::getline(in, line)) {
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        std::string k = line.substr(0, c);
        for (auto& ch : k) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (k == name) {
            std::string v = line.substr(c + 1);
            while (!v.empty() && (v.front() == ' ')) v.erase(v.begin());
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
            return v;
        }
    }
    return "";
}

const char* content_type(const std::string& path) {
    auto ends = [&](const char* s) {
        size_t n = std::strlen(s);
        return path.size() >= n && path.compare(path.size() - n, n, s) == 0;
    };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".js")) return "text/javascript; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".png")) return "image/png";
    return "application/octet-stream";
}

}  // namespace

bool WsServer::start(const std::string& bind_addr, int port, const std::string& ui_dir) {
    ui_dir_ = ui_dir;
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int one = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, bind_addr.c_str(), &sa.sin_addr);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&sa), sizeof sa) != 0 || ::listen(listen_fd_, 8) != 0) {
        LOG("UI server: cannot listen on %s:%d", bind_addr.c_str(), port);
        return false;
    }
    thread_ = std::thread([this] { accept_loop(); });
    thread_.detach();
    return true;
}

void WsServer::accept_loop() {
    for (;;) {
        int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) continue;
        std::thread([this, fd] { handle_connection(fd); }).detach();
    }
}

void WsServer::handle_connection(int fd) {
    std::string req;
    char buf[4096];
    while (req.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0 || req.size() > 65536) {
            ::close(fd);
            return;
        }
        req.append(buf, static_cast<size_t>(n));
    }
    std::string path = req.substr(req.find(' ') + 1);
    path = path.substr(0, path.find(' '));
    if (path.rfind("/ws", 0) == 0 && !header_value(req, "sec-websocket-key").empty()) {
        std::string accept = base64(sha1(header_value(req, "sec-websocket-key") + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
        std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
        write_all(fd, resp.data(), resp.size());
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        int old = client_fd_.exchange(fd);
        if (old >= 0) ::shutdown(old, SHUT_RDWR);
        if (on_connect) on_connect();
        client_loop(fd);
        return;
    }
    serve_file(fd, path);
}

void WsServer::serve_file(int fd, std::string path) {
    path = path.substr(0, path.find('?'));
    if (path == "/") path = "/index.html";
    std::string resp;
    if (path.find("..") != std::string::npos) {
        resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    } else {
        std::ifstream in(ui_dir_ + path, std::ios::binary);
        if (!in) {
            resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        } else {
            std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            resp = "HTTP/1.1 200 OK\r\nContent-Type: " + std::string(content_type(path)) +
                   "\r\nCache-Control: no-store\r\nContent-Length: " + std::to_string(body.size()) +
                   "\r\nConnection: close\r\n\r\n" + body;
        }
    }
    write_all(fd, resp.data(), resp.size());
    ::close(fd);
}

void WsServer::client_loop(int fd) {
    std::string message;
    for (;;) {
        uint8_t hdr[2];
        if (!read_all(fd, hdr, 2)) break;
        uint8_t opcode = hdr[0] & 0x0F;
        bool fin = hdr[0] & 0x80;
        uint64_t len = hdr[1] & 0x7F;
        if (len == 126) {
            uint8_t ext[2];
            if (!read_all(fd, ext, 2)) break;
            len = (ext[0] << 8) | ext[1];
        } else if (len == 127) {
            uint8_t ext[8];
            if (!read_all(fd, ext, 8)) break;
            len = 0;
            for (int i = 0; i < 8; i++) len = (len << 8) | ext[i];
        }
        if (len > (16u << 20)) break;
        uint8_t mask[4] = {};
        if ((hdr[1] & 0x80) && !read_all(fd, mask, 4)) break;
        std::vector<uint8_t> payload(len);
        if (len && !read_all(fd, payload.data(), len)) break;
        for (size_t i = 0; i < len; i++) payload[i] ^= mask[i % 4];
        if (opcode == 0x8) break;  // close
        if (opcode == 0x9) {       // ping -> pong
            std::lock_guard<std::mutex> g(send_mutex_);
            send_frame(fd, 0xA, payload.data(), payload.size());
            continue;
        }
        if (opcode == 0x1 || opcode == 0x0) {
            message.append(payload.begin(), payload.end());
            if (fin) {
                if (on_message) on_message(message);
                message.clear();
            }
        }
    }
    int expected = fd;
    client_fd_.compare_exchange_strong(expected, -1);
    ::close(fd);
}

bool WsServer::send_frame(int fd, uint8_t opcode, const void* data, size_t size) {
    uint8_t hdr[10];
    size_t h = 0;
    hdr[h++] = 0x80 | opcode;
    if (size < 126) {
        hdr[h++] = static_cast<uint8_t>(size);
    } else if (size < 65536) {
        hdr[h++] = 126, hdr[h++] = static_cast<uint8_t>(size >> 8), hdr[h++] = static_cast<uint8_t>(size);
    } else {
        hdr[h++] = 127;
        for (int i = 7; i >= 0; i--) hdr[h++] = static_cast<uint8_t>(static_cast<uint64_t>(size) >> (i * 8));
    }
    return write_all(fd, hdr, h) && write_all(fd, data, size);
}

void WsServer::send_text(const std::string& text) {
    int fd = client_fd_.load();
    if (fd < 0) return;
    std::lock_guard<std::mutex> g(send_mutex_);
    send_frame(fd, 0x1, text.data(), text.size());
}

void WsServer::send_binary(const void* data, size_t size) {
    int fd = client_fd_.load();
    if (fd < 0) return;
    std::lock_guard<std::mutex> g(send_mutex_);
    send_frame(fd, 0x2, data, size);
}

}  // namespace nxb
