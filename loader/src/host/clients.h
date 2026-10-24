#pragma once

// Host implementations of the WKC client interfaces. Everything not overridden here falls back to
// the generated no-op stubs (build/generated/stub_clients.h).

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cfi_gate.h"
#include "generated/stub_clients.h"

namespace nxb {

struct ViewConfig {
    int width = 1280;
    int height = 720;
    std::string user_agent;
    bool handheld = false;  // device mode for the Switch-specific media feature (false = docked/console)
    std::string language = "en-US";  // navigator.language
    std::string user_style_sheet;    // URL (usually data:) of the user style sheet, or empty
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
#if NXB_CORE == 3
    void addMessageWithArgumentsToConsole(WKC::MessageSource, WKC::MessageLevel level, const WKC::String& message,
                                          const std::vector<WKC::String>& arguments, unsigned int lineNumber,
                                          unsigned columnNumber, const WKC::String& sourceID) override;
#endif
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
    // core_3 reports a console API call twice: addMessageToConsole with the first argument as
    // text, then addMessageWithArgumentsToConsole with the rest. The first is held here until the
    // second replaces it (or, failing that, until the run loop flushes it).
    struct PendingConsole {
        bool active = false;
        std::string message, source;
        unsigned line = 0;
        int level = 0;
    };
    void flush_console();
    PendingConsole pending_console_;

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
    // Same-document navigations: fragment changes and the History API (push/replace/back/forward).
    void dispatchDidChangeLocationWithinPage() override {
        if (events_.location_changed) events_.location_changed();
    }
    void dispatchDidPushStateWithinPage() override {
        if (events_.location_changed) events_.location_changed();
    }
    void dispatchDidReplaceStateWithinPage() override {
        if (events_.location_changed) events_.location_changed();
    }
    void dispatchDidPopStateWithinPage() override {
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

// Web Storage (localStorage / sessionStorage) for one origin. The engine keeps no storage itself;
// without a working client sessionStorage drops every write and localStorage is null. Items live in
// memory, shared by every area created for the same origin and type (so they survive navigations
// within this process), and are lost on exit.
class HostStorageArea : public WKC::StubStorageAreaIf {
public:
    using Items = std::map<std::u16string, std::u16string>;
    HostStorageArea(std::shared_ptr<Items> items, unsigned quota) : items_(std::move(items)), quota_(quota) {}

    unsigned length() override { return static_cast<unsigned>(items_->size()); }
    WKC::String key(unsigned index) override;
    WKC::String item(const WKC::String& key) override;
    void setItem(WKC::Frame*, const WKC::String& key, const WKC::String& value, bool& quotaException) override;
    void removeItem(WKC::Frame*, const WKC::String& key) override { items_->erase(to_u16(key)); }
    void clear(WKC::Frame*) override { items_->clear(); }
    bool contains(const WKC::String& key) override { return items_->count(to_u16(key)) != 0; }
#if NXB_CORE == 0
    bool canAccessStorage(WKC::Frame*) override { return true; }
#endif
    size_t memoryBytesUsedByCache() override { return 0; }
    void incrementAccessCount() override {}
    void decrementAccessCount() override {}
    void closeDatabaseIfIdle() override {}

    // The shared item map for an origin and WKC::StorageType.
    static std::shared_ptr<Items> items_for(const WKC::String& origin, int type);

private:
    static std::u16string to_u16(const WKC::String& s);
    static WKC::String to_wkc(const std::u16string& s);
    std::shared_ptr<Items> items_;
    unsigned quota_;  // bytes (UTF-16), 0 = unlimited
};

// Session history (the back/forward list). The engine keeps none itself: it hands every new entry
// to the embedder and asks it for entries relative to the current one (history.length, back/forward,
// history.go). Modelled on WebKit's BackForwardList: adding an entry drops everything forward of
// the current one, and the oldest entry goes once the list is full.
class HostBackForwardList : public WKC::StubBackForwardClientIf {
public:
    ~HostBackForwardList() { close(); }
    void addItem(WKC::HistoryItem* item) override;
    void goToItem(WKC::HistoryItem* item) override;
    WKC::HistoryItem* itemAtIndex(int index) override;  // relative to the current entry
    int backListCount() override { return current_ < 0 ? 0 : current_; }
    int forwardListCount() override { return current_ < 0 ? 0 : static_cast<int>(items_.size()) - 1 - current_; }
    void close() override;

private:
    static constexpr size_t kCapacity = 100;  // WebKit's default
    std::vector<WKC::HistoryItem*> items_;    // our own references (HistoryItem::create(item, true))
    int current_ = -1;
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
    WKC::BackForwardClientIf* createBackForwardClient(WKC::WKCWebView*) override {
        return gate_vtable(new HostBackForwardList());
    }
    void deleteBackForwardClient(WKC::BackForwardClientIf* c) override { delete static_cast<HostBackForwardList*>(c); }
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
    WKC::StorageAreaIf* createStorageAreaClient(WKC::WKCWebView*, const WKC::String& origin, WKC::StorageType type,
                                                unsigned int quota) override {
        return gate_vtable(new HostStorageArea(HostStorageArea::items_for(origin, type), quota));
    }
    void deleteStorageAreaClient(WKC::StorageAreaIf* c) override { delete static_cast<HostStorageArea*>(c); }

private:
    const ViewConfig& config_;
    ViewEvents& events_;
};

}  // namespace nxb
