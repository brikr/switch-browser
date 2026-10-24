#include "host/browser.h"

#include <JavaScriptCore/JavaScript.h>

#include <pthread.h>

#include <cstdlib>

#include "cfi_gate.h"
#include "host/media_procs.h"
#include "host/run_loop.h"
#include "log.h"

namespace nxb {

bool Browser::initialize(size_t heap_bytes, size_t font_heap_bytes) {
    heap_ = std::aligned_alloc(4096, heap_bytes);
    font_heap_ = std::aligned_alloc(4096, font_heap_bytes);
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
    return ok;
}

bool Browser::create_view(const ViewConfig& config) {
    config_ = config;
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
