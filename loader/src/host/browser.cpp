#include "host/browser.h"

#include <JavaScriptCore/JavaScript.h>

#include <pthread.h>

#include <dirent.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <string>
#include <fstream>
#include <iterator>

#include "cfi_gate.h"
#include "host/file_procs.h"
#include "host/media_procs.h"
#include "host/png.h"
#include "host/run_loop.h"
#include "log.h"

namespace nxb {

// Wall-clock time for the engine (Date, certificate validity). Without it the clock starts at 1970.
static void current_posix_time(wkc_int64* out) { *out = static_cast<wkc_int64>(std::time(nullptr)); }

#if NXB_CORE == 3
// core_3's network peer refuses DNS lookups unless this callback exists and returns true.
static bool network_available() { return true; }
#endif

bool Browser::initialize(size_t heap_bytes, size_t font_heap_bytes) {
    heap_ = std::aligned_alloc(4096, heap_bytes);
    font_heap_bytes = std::max<size_t>(font_heap_bytes, WKC::WKCWebKitFontHeapSize());
    font_heap_ = std::aligned_alloc(4096, (font_heap_bytes + 4095) & ~size_t(4095));
    if (!heap_ || !font_heap_) FATAL("cannot allocate engine heaps");
    // The peer layer needs the main thread and its stack registered first (it fails init
    // otherwise); JavaScriptCore uses the stack bounds for its overflow checks.
    pthread_attr_t attr;
    void* stack_low = nullptr;
    size_t stack_size = 0;
    if (pthread_getattr_np(pthread_self(), &attr) != 0 || pthread_attr_getstack(&attr, &stack_low, &stack_size) != 0)
        FATAL("cannot query the main thread stack");
    pthread_attr_destroy(&attr);
    void* stack_base = static_cast<char*>(stack_low) + stack_size;  // stacks grow down
    LOG("main thread stack %p..%p (%zu KB)", stack_low, stack_base, stack_size >> 10);
    WKC::WKCPrefs::setThreadInfo(reinterpret_cast<void*>(pthread_self()), stack_base);
    WKC::WKCPrefs::setStackSize(static_cast<unsigned>(stack_size));
    // Read once during initialisation (the peer keeps base time + elapsed ticks), so register first.
    WKC::WKCWebkitRegisterGetCurrentTimeCallback(gate_function(&current_posix_time));
    WKC::WKCWebKitSetMediaPlayerProcs(static_cast<const WKC::MediaPlayerProcs*>(media_player_procs()));

    LOG("WKCWebKitInitialize(heap %zu MB, font heap %zu KB)", heap_bytes >> 20, font_heap_bytes >> 10);
    // virtual_memory_size = 0: "physical" mode, the engine sub-allocates from `heap_` directly.
    gate_vtable(&memory_handler_);
#if NXB_CORE == 0
    gate_vtable(&timer_handler_);
    bool ok = WKC::WKCWebKitInitialize(heap_, heap_bytes, 0, font_heap_, font_heap_bytes, memory_handler_,
                                       timer_handler_);
#else
    bool ok = WKC::WKCWebKitInitialize(heap_, heap_bytes, 0, font_heap_, font_heap_bytes, memory_handler_);
#endif
    LOG("WKCWebKitInitialize -> %s", ok ? "ok" : "FAILED");
    if (ok) RunLoop::main().attach_engine();
    if (ok) install_file_procs();
#if NXB_CORE == 3
    if (ok) WKC::WKCWebKitSetIsNetworkAvailableProc(gate_function(&network_available));
#endif
    return ok;
}

void Browser::set_system_strings(const ViewConfig& config) {
    static std::vector<std::u16string> storage;  // the engine keeps the pointers
    auto u16 = [](const std::string& s) {
        storage.emplace_back(s.begin(), s.end());  // ASCII values only
        return reinterpret_cast<const unsigned short*>(storage.back().c_str());
    };
    storage.reserve(16);
    WKC::SystemStrings strings{};
    // platform/vendor: unknown on a real Switch, so left empty (what the engine reports by default).
    strings.fNavigatorPlatform = u16("");
    strings.fNavigatorProduct = u16("Gecko");        // fixed by the HTML spec for all browsers
    strings.fNavigatorProductSub = u16("20030107");  // ditto (WebKit/Blink value)
    strings.fNavigatorVendor = u16("");
    strings.fNavigatorVendorSub = u16("");
    strings.fLanguage = u16(config.language);
    strings.fButtonLabelSubmit = u16("Submit");
    strings.fButtonLabelReset = u16("Reset");
    strings.fButtonLabelFile = u16("Choose File");
    WKC::WKCPrefs::setSystemStrings(&strings);
}

bool Browser::create_view(const ViewConfig& config) {
    config_ = config;
    set_system_strings(config);
    WKC::WKCWebKitSetDeviceMode(config.handheld ? WKC::EDeviceModeHandheld : WKC::EDeviceModeConsole);
    builders_ = std::make_unique<HostClientBuilders>(config_, events);
    gate_vtable(builders_.get());
    view_ = WKC::WKCWebView::create(*builders_);
    if (!view_) {
        LOG("WKCWebView::create failed");
        return false;
    }
    bitmap_.assign(static_cast<size_t>(config.width) * config.height, 0xFFFFFFFF);
    WKCSize size{config.width, config.height};
    bool ok = view_->setOffscreen(WKC::EOffscreenFormatCairo32, bitmap_.data(), config.width * 4, size, size, false);
    LOG("WebView created (%dx%d), setOffscreen -> %s", config.width, config.height, ok ? "ok" : "FAILED");
    WKC::WKCSettings* settings = view_->settings();
    settings->setLocalStorageEnabled(true);  // off by default (localStorage is null); see HostStorageArea
    if (!config.user_style_sheet.empty()) settings->setUserStyleSheetLocation(config.user_style_sheet.c_str());
    view_->notifyFocusIn();  // the view must have focus to receive keyboard input
    return ok;
}

int Browser::register_fonts(const std::string& dir) {
    std::vector<std::string> names;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".ttf") == 0) names.push_back(n);
        }
        closedir(d);
    }
    // Primary first: the standard (Latin/Japanese) face, then Nintendo symbols, then the CJK sets.
    auto rank = [](const std::string& n) {
        static const char* order[] = {"_std_", "nintendo_ext_", "nintendo_ext2_", "_ko_", "_org_zh-cn_", "_ext_zh-cn_",
                                      "_zh-tw_"};
        for (int i = 0; i < 7; i++)
            if (n.find(order[i]) != std::string::npos) return i;
        return 7;
    };
    std::sort(names.begin(), names.end(), [&](const std::string& a, const std::string& b) {
        return rank(a) != rank(b) ? rank(a) < rank(b) : a < b;
    });
    int count = 0;
    for (auto& n : names) {
        std::ifstream in(dir + "/" + n, std::ios::binary);
        fonts_.emplace_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto& data = fonts_.back();
        int id = WKC::WKCWebKitRegisterFontOnMemory(data.data(), static_cast<unsigned>(data.size()));
        LOG("font %s (%zu KB) -> id %d", n.c_str(), data.size() >> 10, id);
        if (id >= 0) count++;
    }
    return count;
}

bool Browser::load_icu_data(const std::string& romfs_dir) {
#if NXB_CORE == 3
    std::string dir = romfs_dir + "/browser";
    std::string file;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.rfind("icudt", 0) == 0 && n.size() > 4 && n.compare(n.size() - 4, 4, ".dat") == 0) file = dir + "/" + n;
        }
        closedir(d);
    }
    if (file.empty()) {
        LOG("warning: no ICU data in %s (re-run prepare_firmware.py); Intl will crash the engine", dir.c_str());
        return false;
    }
    std::ifstream in(file, std::ios::binary);
    icu_data_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    bool ok = WKC::WKCWebKitSetICUData(icu_data_.data());
    LOG("ICU data %s (%zu KB) -> %s", file.c_str(), icu_data_.size() >> 10, ok ? "ok" : "FAILED");
    return ok;
#else
    (void)romfs_dir;
    return true;  // core_0 has no Intl
#endif
}

int Browser::register_root_cas(const std::string& dir) {
    int count = 0, failed = 0;
    DIR* d = opendir(dir.c_str());
    if (!d) return 0;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() < 5 || n.compare(n.size() - 4, 4, ".pem") != 0) continue;
        std::ifstream in(dir + "/" + n);
        std::string pem((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        // One certificate per call.
        const std::string end_marker = "-----END CERTIFICATE-----";
        for (size_t pos = 0;;) {
            size_t b = pem.find("-----BEGIN CERTIFICATE-----", pos);
            if (b == std::string::npos) break;
            size_t e2 = pem.find(end_marker, b);
            if (e2 == std::string::npos) break;
            e2 += end_marker.size();
            std::string one = pem.substr(b, e2 - b) + "\n";
            if (WKC::WKCWebKitSSLRegisterRootCA(one.c_str(), static_cast<int>(one.size()))) count++;
            else failed++;
            pos = e2;
        }
    }
    closedir(d);
    LOG("registered %d root CAs from %s (%d failed)", count, dir.c_str(), failed);
    return count;
}

// The engine paints only what the page draws; pages without a background would otherwise show
// whatever the previous page left in the bitmap. Browsers show white there.
void Browser::clear_bitmap(const WKCRect& r) {
    for (int y = r.fY; y < r.fY + r.fHeight; y++)
        std::fill_n(bitmap_.data() + static_cast<size_t>(y) * config_.width + r.fX, r.fWidth, 0xFFFFFFFFu);
}

bool Browser::screenshot(const std::string& png_path) {
    WKCRect all{0, 0, config_.width, config_.height};
    clear_bitmap(all);
    view_->notifyPaintOffscreen(all);
    std::vector<uint8_t> rgba(static_cast<size_t>(config_.width) * config_.height * 4);
    cairo_argb32_to_rgba(bitmap_.data(), config_.width, config_.height, config_.width, rgba.data());
    bool ok = write_png(png_path, config_.width, config_.height, rgba.data());
    LOG("screenshot %s -> %s", png_path.c_str(), ok ? "ok" : "FAILED");
    return ok;
}

void Browser::mouse(MouseKind kind, int x, int y, int button, int mods) {
    WKCPoint pos{x, y};
    auto b = static_cast<WKC::MouseButton>(button == 0 ? WKC::EMouseButtonLeft
                                            : button == 1 ? WKC::EMouseButtonMiddle : WKC::EMouseButtonRight);
    auto m = static_cast<WKC::Modifier>(mods);
    switch (kind) {
        case MouseKind::Down: view_->notifyMouseDown(pos, b, m); break;
        case MouseKind::Up: view_->notifyMouseUp(pos, b, m); break;
        case MouseKind::Move: view_->notifyMouseMove(pos, WKC::EMouseButtonNone, m); break;
        case MouseKind::DoubleClick: view_->notifyMouseDoubleClick(pos, b, m); break;
    }
}

void Browser::wheel(int x, int y, float dx, float dy, int mods) {
    WKCPoint pos{x, y};
#if NXB_CORE == 3
    WKCFloatSize diff{dx, dy};
#else
    WKCSize diff{static_cast<int>(dx), static_cast<int>(dy)};
#endif
    // notifyMouseWheel only dispatches the DOM wheel event (true = the page cancelled it); the
    // embedder scrolls the view itself, like the Switch applet does for stick/touch input.
    // WebKit deltas are positive towards the top/left; notifyScroll takes positive = down/right.
    bool cancelled = view_->notifyMouseWheel(pos, diff, static_cast<WKC::Modifier>(mods));
    if (!cancelled) view_->notifyScroll(static_cast<int>(-dx), static_cast<int>(-dy));
    TRACE("wheel(%d,%d d=%.0f,%.0f) cancelled=%d", x, y, dx, dy, cancelled);
}

void Browser::key(bool down, int key_code, int mods, bool repeat) {
    auto k = static_cast<WKC::Key>(key_code);
    auto m = static_cast<WKC::Modifier>(mods);
#if NXB_CORE == 3
    bool handled = view_->notifyKeyEvent(down ? WKC::EKeyEventDown : WKC::EKeyEventUp, k, 0, m, repeat);
    TRACE("key(%s %d) -> %d", down ? "down" : "up", key_code, handled);
#else
    if (down) view_->notifyKeyPress(k, m, repeat);
    else view_->notifyKeyRelease(k, m);
#endif
}

void Browser::text(unsigned codepoint) {
#if NXB_CORE == 3
    bool handled = view_->notifyKeyEvent(WKC::EKeyEventPress, static_cast<WKC::Key>(0), codepoint, WKC::EModifierNone, false);
    TRACE("text(%u) -> %d", codepoint, handled);
#else
    view_->notifyKeyChar(codepoint);
#endif
}

void Browser::go_back_or_forward(int steps) {
    if (view_->canGoBackOrForward(steps)) view_->goBackOrForward(steps);
}
bool Browser::can_go_back_or_forward(int steps) { return view_->canGoBackOrForward(steps); }
void Browser::reload() { view_->reload(); }
void Browser::stop() { view_->stopLoading(); }

void Browser::set_device_mode(bool handheld) {
    config_.handheld = handheld;
    WKC::WKCWebKitSetDeviceMode(handheld ? WKC::EDeviceModeHandheld : WKC::EDeviceModeConsole);
    view_->notifyRelayout(true);
    if (events.repaint) events.repaint(WKCRect{0, 0, config_.width, config_.height});
}

void Browser::popup_choose(int index) {
    if (HostDropDownListClient* dropdown = builders_ ? builders_->dropdown() : nullptr) dropdown->choose(index);
}

std::string Browser::current_url() {
    const char* u = view_->uri();
    return u ? u : "";
}

std::vector<uint8_t> Browser::paint_rgba(WKCRect& rect) {
    int x0 = std::max(0, rect.fX), y0 = std::max(0, rect.fY);
    int x1 = std::min(config_.width, rect.fX + rect.fWidth), y1 = std::min(config_.height, rect.fY + rect.fHeight);
    rect = WKCRect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
    std::vector<uint8_t> out(static_cast<size_t>(rect.fWidth) * rect.fHeight * 4);
    if (out.empty()) return out;
    clear_bitmap(rect);
    view_->notifyPaintOffscreen(rect);
    cairo_argb32_to_rgba(bitmap_.data() + static_cast<size_t>(y0) * config_.width + x0, rect.fWidth, rect.fHeight,
                         config_.width, out.data());
    return out;
}

void Browser::load_html(const std::string& html, const std::string& base_uri) {
    view_->loadHTMLString(html.c_str(), base_uri.c_str());
}

bool Browser::load_url(const std::string& url) { return view_->loadURI(url.c_str()); }

static std::string js_to_string(JSContextRef ctx, JSValueRef value) {
    if (!value) return "(null)";
    JSStringRef s = JSValueToStringCopy(ctx, value, nullptr);
    if (!s) return "(unconvertible)";
    size_t max = JSStringGetMaximumUTF8CStringSize(s);
    std::string out(max, '\0');
    size_t n = JSStringGetUTF8CString(s, &out[0], max);
    out.resize(n ? n - 1 : 0);
    JSStringRelease(s);
    return out;
}

std::string Browser::evaluate(const std::string& script, bool* threw) {
    JSGlobalContextRef ctx = view_->mainFrame()->globalContext();
    if (!ctx) FATAL("main frame has no JS context");
    JSStringRef src = JSStringCreateWithUTF8CString(script.c_str());
    JSValueRef exception = nullptr;
    JSValueRef result = JSEvaluateScript(ctx, src, nullptr, nullptr, 1, &exception);
    JSStringRelease(src);
    if (threw) *threw = exception != nullptr;
    return js_to_string(ctx, exception ? exception : result);
}

}  // namespace nxb
