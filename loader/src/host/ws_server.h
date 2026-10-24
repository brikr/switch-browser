#pragma once

// Minimal HTTP + WebSocket server for the browser UI (no dependencies).
//  - GET /ws with an Upgrade header becomes the (single) WebSocket client; a new client replaces
//    the old one.
//  - Any other GET serves a file from the UI directory.
// Runs on its own thread; text messages from the client are delivered to `on_message` on that
// thread (callers post work to the engine's run loop). send_* are thread-safe.

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace nxb {

class WsServer {
public:
    std::function<void(const std::string&)> on_message;
    std::function<void()> on_connect;

    // Binds and starts the accept thread. Returns false if the port can't be bound.
    bool start(const std::string& bind_addr, int port, const std::string& ui_dir);

    bool connected() const { return client_fd_.load() >= 0; }
    void send_text(const std::string& text);
    void send_binary(const void* data, size_t size);

private:
    void accept_loop();
    void handle_connection(int fd);
    void serve_file(int fd, std::string path);
    void client_loop(int fd);
    bool send_frame(int fd, uint8_t opcode, const void* data, size_t size);

    std::string ui_dir_;
    int listen_fd_ = -1;
    std::atomic<int> client_fd_{-1};
    std::mutex send_mutex_;
    std::thread thread_;
};

}  // namespace nxb
