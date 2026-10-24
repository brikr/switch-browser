#include "host/browser.h"

#include <JavaScriptCore/JavaScript.h>

#include <pthread.h>

#include <dirent.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
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

bool Browser::create_view(const ViewConfig& config) {
    config_ = config;
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

bool Browser::screenshot(const std::string& png_path) {
    WKCRect all{0, 0, config_.width, config_.height};
    view_->notifyPaintOffscreen(all);
    std::vector<uint8_t> rgba(static_cast<size_t>(config_.width) * config_.height * 4);
    cairo_argb32_to_rgba(bitmap_.data(), config_.width, config_.height, config_.width, rgba.data());
    bool ok = write_png(png_path, config_.width, config_.height, rgba.data());
    LOG("screenshot %s -> %s", png_path.c_str(), ok ? "ok" : "FAILED");
    return ok;
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
