#pragma once

// Host implementations of the WKC client interfaces. Everything not overridden here falls back to
// the generated no-op stubs (build/generated/stub_clients.h).

#include <functional>
#include <string>

#include "cfi_gate.h"
#include "generated/stub_clients.h"

namespace nxb {

struct ViewConfig {
    int width = 1280;
    int height = 720;
    std::string user_agent;
    bool handheld = false;  // device mode for the Switch-specific media feature (false = docked/console)
    std::string language = "en-US";  // navigator.language
};

// Events the embedding application cares about.
struct ViewEvents {
    std::function<void(const std::string& message, unsigned line, const std::string& source, int level)> console;
    std::function<void(const std::string& message)> alert;
    std::function<void()> load_finished;
    std::function<void(const std::string& url, const std::string& description)> load_failed;
    std::function<void(const std::string& title)> title_changed;
    std::function<void(const WKCRect& rect)> repaint;  // area of the view that needs painting
    std::function<void()> load_started;
    std::function<void()> location_changed;            // committed load or same-document navigation
};

std::string to_std(const WKC::String& s);

class HostChromeClient : public WKC::StubChromeClientIf {
public:
    HostChromeClient(const ViewConfig& config, ViewEvents& events) : config_(config), events_(events) {}

    WKCFloatRect windowRect() override;
    WKCFloatRect pageRect() override;
    void addMessageToConsole(WKC::MessageSource, WKC::MessageType, WKC::MessageLevel level, const WKC::String& message,
                             unsigned int lineNumber, const WKC::String& sourceID) override;
    void runJavaScriptAlert(WKC::Frame*, const WKC::String& message) override;
    bool runJavaScriptConfirm(WKC::Frame*, const WKC::String& message) override;
    bool runJavaScriptPrompt(WKC::Frame*, const WKC::String& message, const WKC::String& defaultValue,
                             WKC::String& result) override;
    WKCPoint screenToWindow(const WKCPoint& p) const override { return p; }
    WKCRect windowToScreen(const WKCRect& r) const override { return r; }
    bool canRunBeforeUnloadConfirmPanel() override { return true; }
    bool runBeforeUnloadConfirmPanel(const WKC::String&, WKC::Frame*) override { return true; }
    bool scrollbarsVisible() override { return true; }
    void repaint(const WKCRect& rect, bool, bool = false, bool = false) override {
        if (events_.repaint) events_.repaint(rect);
    }
    void scroll(const WKCSize&, const WKCRect&, const WKCRect& clipRect) override {
        if (events_.repaint) events_.repaint(clipRect);
    }
    unsigned int underlayColor() const override { return 0xFFFFFFFF; }

private:
    const ViewConfig& config_;
    ViewEvents& events_;
};

class HostFrameLoaderClient : public WKC::StubFrameLoaderClientIf {
public:
    HostFrameLoaderClient(WKC::WKCWebFrame* frame, const ViewConfig& config, ViewEvents& events)
        : frame_(frame), config_(config), events_(events) {}

    bool hasWebView() const override { return true; }
    bool canHandleRequest(const WKC::ResourceRequest&) const override { return true; }
    bool canShowMIMEType(const WKC::String&) const override { return true; }
    bool canShowMIMETypeAsHTML(const WKC::String&) const override { return true; }
    bool allowScript(bool enabledPerSettings) override { return enabledPerSettings; }
    bool allowWebGL(bool enabledPerSettings) override { return enabledPerSettings; }
    bool shouldGoToHistoryItem(WKC::HistoryItem*) const override { return true; }
#if NXB_CORE == 0
    bool dispatchWillAcceptCookie(bool, WKC::ResourceHandle*, const char*, const char*, const char*) override { return true; }
#endif
    bool dispatchWillReceiveData(WKC::ResourceHandle*, int) override { return true; }
    // true would mean "reload the request" (the engine retries forever).
    bool notifySSLHandshakeStatus(WKC::ResourceHandle*, WKC::SSLHandshakeStatus) override { return false; }
    int dispatchWillPermitSendRequest(void*, const char*, WKC::ContentComposition, bool,
                                      const WKC::ResourceResponse&) override { return 1; }  // allow
    WKC::String userAgent(const WKC::KURL&) override;

    // Navigation policy: allow everything.
#if NXB_CORE == 0
    void dispatchDecidePolicyForNewWindowAction(WKC::FramePolicyFunction& f, const WKC::NavigationAction&,
                                                const WKC::ResourceRequest&, WKC::FormState*,
                                                const WKC::String&) override { f.reply(WKC::PolicyUse); }
    void dispatchDecidePolicyForNavigationAction(WKC::FramePolicyFunction& f, const WKC::NavigationAction&,
                                                 const WKC::ResourceRequest&, WKC::FormState*) override {
        f.reply(WKC::PolicyUse);
    }
    void dispatchDecidePolicyForResponse(WKC::FramePolicyFunction& f, const WKC::ResourceResponse&,
                                         const WKC::ResourceRequest&) override { f.reply(WKC::PolicyUse); }
    void dispatchWillSubmitForm(WKC::FramePolicyFunction& f, WKC::FormState*) override { f.reply(WKC::PolicyUse); }
#else
    void dispatchDecidePolicyForNewWindowAction(WKC::FramePolicyFunction& f, const WKC::NavigationAction&,
                                                const WKC::ResourceRequest&, WKC::FormState*, const WKC::String&,
                                                const WKC::PolicyCheckIdentifier& id) override {
        f.reply(WKC::PolicyAction::Use, id);
    }
    void dispatchDecidePolicyForNavigationAction(WKC::FramePolicyFunction& f, const WKC::NavigationAction&,
                                                 const WKC::ResourceRequest&, const WKC::ResourceResponse&,
                                                 WKC::FormState*, const WKC::PolicyCheckIdentifier& id) override {
        f.reply(WKC::PolicyAction::Use, id);
    }
    void dispatchDecidePolicyForResponse(WKC::FramePolicyFunction& f, const WKC::ResourceResponse&,
                                         const WKC::ResourceRequest&, const WKC::PolicyCheckIdentifier& id) override {
        f.reply(WKC::PolicyAction::Use, id);
    }
#endif

    void dispatchDidStartProvisionalLoad() override {
        if (events_.load_started) events_.load_started();
    }
    void dispatchDidCommitLoad() override {
        if (events_.location_changed) events_.location_changed();
    }
    void dispatchDidChangeLocationWithinPage() override {
        if (events_.location_changed) events_.location_changed();
    }
    void dispatchDidFinishLoad() override;
    void dispatchDidFailLoad(const WKC::ResourceError& e) override;
    void dispatchDidFailProvisionalLoad(const WKC::ResourceError& e) override;
    void dispatchDidReceiveTitle(const WKC::String& title) override;

private:
    WKC::WKCWebFrame* frame_;
    const ViewConfig& config_;
    ViewEvents& events_;
};

// Turns keyboard events into editing: character input -> Editor::insertText, editing keys ->
// WebKit editor commands. (WKC leaves this to the embedder, like WebKit's other ports.)
class HostEditorClient : public WKC::StubEditorClientIf {
public:
    void handleKeyboardEvent(WKC::KeyboardEvent* event) override;
};

// Creates the client objects for each WebView / frame. Every object handed to the engine has its
// vtable gated (cfi_gate.h). Most interfaces have non-virtual destructors,
// so deletes go through the concrete type that was created.
class HostClientBuilders : public WKC::WKCClientBuilders {
public:
    HostClientBuilders(const ViewConfig& config, ViewEvents& events) : config_(config), events_(events) {}

    WKC::ChromeClientIf* createChromeClient(WKC::WKCWebView*) override {
        return gate_vtable(new HostChromeClient(config_, events_));
    }
    void deleteChromeClient(WKC::ChromeClientIf* c) override { delete static_cast<HostChromeClient*>(c); }
    WKC::FrameLoaderClientIf* createFrameLoaderClient(WKC::WKCWebFrame* frame) override {
        return gate_vtable(new HostFrameLoaderClient(frame, config_, events_));
    }
    void deleteFrameLoaderClient(WKC::FrameLoaderClientIf* c) override {
        delete static_cast<HostFrameLoaderClient*>(c);
    }

#define NXB_STUB_CLIENT(Type, Name)                                                         \
    WKC::Type* create##Name(WKC::WKCWebView*) override { return gate_vtable(new WKC::Stub##Type()); } \
    void delete##Name(WKC::Type* c) override { delete static_cast<WKC::Stub##Type*>(c); }
    NXB_STUB_CLIENT(ContextMenuClientIf, ContextMenuClient)
    NXB_STUB_CLIENT(DragClientIf, DragClient)
    WKC::EditorClientIf* createEditorClient(WKC::WKCWebView*) override { return gate_vtable(new HostEditorClient()); }
    void deleteEditorClient(WKC::EditorClientIf* c) override { delete static_cast<HostEditorClient*>(c); }
    NXB_STUB_CLIENT(InspectorClientIf, InspectorClient)
    NXB_STUB_CLIENT(DropDownListClientIf, DropDownListClient)
    NXB_STUB_CLIENT(GeolocationClientIf, GeolocationClient)
    NXB_STUB_CLIENT(DeviceMotionClientIf, DeviceMotionClient)
    NXB_STUB_CLIENT(DeviceOrientationClientIf, DeviceOrientationClient)
    NXB_STUB_CLIENT(BackForwardClientIf, BackForwardClient)
#if NXB_CORE == 0
    NXB_STUB_CLIENT(BatteryClientIf, BatteryClient)
    NXB_STUB_CLIENT(NavigatorContentUtilsClientIf, NavigatorContentUtilsClient)
#endif
    NXB_STUB_CLIENT(ProgressTrackerClientIf, ProgressTrackerClient)
    NXB_STUB_CLIENT(WebNfcClientIf, WebNfcClient)
    NXB_STUB_CLIENT(VibrationClientIf, VibrationClient)
#undef NXB_STUB_CLIENT

    void notifyDeviceMotionClientDestroyed(WKC::DeviceMotionClientIf*) override {}
    void notifyDeviceOrientationClientDestroyed(WKC::DeviceOrientationClientIf*) override {}
    WKC::StorageAreaIf* createStorageAreaClient(WKC::WKCWebView*, const WKC::String&, WKC::StorageType,
                                                unsigned int) override {
        return gate_vtable(new WKC::StubStorageAreaIf());
    }
    void deleteStorageAreaClient(WKC::StorageAreaIf* c) override { delete static_cast<WKC::StubStorageAreaIf*>(c); }

private:
    const ViewConfig& config_;
    ViewEvents& events_;
};

}  // namespace nxb
