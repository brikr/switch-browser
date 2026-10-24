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
    // Sets navigator.* strings and form button labels (WKCPrefs::setSystemStrings). Without this,
    // navigator.language reads as "e" and the other navigator strings are empty.
    void set_system_strings(const ViewConfig& config);
    // Creates the WebView rendering into an internal 32-bit bitmap of config.width x config.height.
    bool create_view(const ViewConfig& config);

    // Registers every .ttf in `dir` (the Switch system fonts from prepare_firmware.py). The standard
    // font is registered first, which makes it the engine's primary font. Returns the count.
    int register_fonts(const std::string& dir);
    // core_3: loads ICU's common data (BrowserDll's browser/icudt*.dat, decompressed by
    // prepare_firmware.py) and hands it to the engine. Without it, any use of Intl crashes the engine.
    bool load_icu_data(const std::string& romfs_dir);
    // Registers every certificate in the .pem files in `dir` as a trusted root CA. Returns the count.
    int register_root_cas(const std::string& dir);
    // Paints the whole view into the offscreen bitmap and writes it as PNG.
    bool screenshot(const std::string& png_path);

    void load_html(const std::string& html, const std::string& base_uri);
    bool load_url(const std::string& url);
    // Evaluates `script` in the main frame. Returns the result (or exception) converted to a string.
    std::string evaluate(const std::string& script, bool* threw = nullptr);

    // Interactive input (all on the loop thread). Coordinates are view pixels; `mods` uses the
    // engine's Modifier bits (1 ctrl, 2 shift, 4 alt); `button` 0/1/2 = left/middle/right.
    enum class MouseKind { Down, Up, Move, DoubleClick };
    void mouse(MouseKind kind, int x, int y, int button, int mods);
    void wheel(int x, int y, float dx, float dy, int mods);
    void key(bool down, int key_code, int mods, bool repeat);  // Windows virtual-key codes
    void text(unsigned codepoint);                             // character input
    void go_back_or_forward(int steps);
    bool can_go_back_or_forward(int steps);
    void reload();
    void stop();
    void set_device_mode(bool handheld);
    std::string current_url();
    // Paints `rect` into the offscreen bitmap and returns it as RGBA8 (rect is clipped to the view).
    std::vector<uint8_t> paint_rgba(WKCRect& rect);
    int width() const { return config_.width; }
    int height() const { return config_.height; }

    ViewEvents events;
    WKC::WKCWebView* view() const { return view_; }
    const std::vector<uint32_t>& bitmap() const { return bitmap_; }

private:
    void clear_bitmap(const WKCRect& r);

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
    std::vector<std::vector<uint8_t>> fonts_;
    std::vector<uint8_t> icu_data_;  // must outlive the engine  // must outlive the engine (registered by pointer)
};

}  // namespace nxb
