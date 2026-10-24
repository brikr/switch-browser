#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "host/clients.h"
#include "host/handlers.h"

namespace nxb {

class Browser {
public:
    // Initialises the engine with a heap of `heap_bytes`. Must be called once, on the loop thread.
    bool initialize(size_t heap_bytes, size_t font_heap_bytes);
    // Creates the WebView rendering into an internal 32-bit bitmap of config.width x config.height.
    bool create_view(const ViewConfig& config);

    // Registers every .ttf in `dir` (the Switch system fonts from prepare_firmware.py). The standard
    // font is registered first, which makes it the engine's primary font. Returns the count.
    int register_fonts(const std::string& dir);
    // Registers every certificate in the .pem files in `dir` as a trusted root CA. Returns the count.
    int register_root_cas(const std::string& dir);
    // Paints the whole view into the offscreen bitmap and writes it as PNG.
    bool screenshot(const std::string& png_path);

    void load_html(const std::string& html, const std::string& base_uri);
    bool load_url(const std::string& url);
    // Evaluates `script` in the main frame. Returns the result (or exception) converted to a string.
    std::string evaluate(const std::string& script, bool* threw = nullptr);

    ViewEvents events;
    WKC::WKCWebView* view() const { return view_; }
    const std::vector<uint32_t>& bitmap() const { return bitmap_; }

private:
    MemoryHandler memory_handler_;
#if NXB_CORE == 0
    TimerHandler timer_handler_;
#endif
    ViewConfig config_;
    std::unique_ptr<HostClientBuilders> builders_;
    WKC::WKCWebView* view_ = nullptr;
    std::vector<uint32_t> bitmap_;
    void* heap_ = nullptr;
    void* font_heap_ = nullptr;
    std::vector<std::vector<uint8_t>> fonts_;  // must outlive the engine (registered by pointer)
};

}  // namespace nxb
