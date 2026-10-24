#include "host/clients.h"

#include <cstdlib>
#include <cstring>

#include <helpers/WKCDocument.h>
#include <helpers/WKCEditor.h>
#include <helpers/WKCEventTarget.h>
#include <helpers/WKCFrame.h>
#include <helpers/WKCFrameView.h>
#include <helpers/WKCHistoryItem.h>
#include <helpers/WKCKeyboardEvent.h>
#include <helpers/WKCNode.h>
#include <helpers/WKCPopupMenuClient.h>
#include <helpers/WKCResourceError.h>

#include "host/run_loop.h"
#include "log.h"

namespace nxb {

std::string to_std(const WKC::String& s) {
    WKC::CString& utf8 = s.utf8();
    return std::string(utf8.data() ? utf8.data() : "", utf8.length());
}

WKCFloatRect HostChromeClient::windowRect() {
    return WKCFloatRect{0, 0, static_cast<float>(config_.width), static_cast<float>(config_.height)};
}
WKCFloatRect HostChromeClient::pageRect() {
    return windowRect();
}

namespace {

#if NXB_CORE == 3
// Applies console format specifiers (%s %d %i %f %o %O %c %%) in `format` using `args`, then
// appends the unused arguments separated by spaces, like browser consoles do. The engine has
// already converted every argument to a string, so objects arrive as "[object Object]".
std::string format_console(const std::string& format, const std::vector<std::string>& args) {
    std::string out;
    size_t next = 0;
    for (size_t i = 0; i < format.size(); i++) {
        char c = format[i];
        if (c != '%' || i + 1 >= format.size()) {
            out += c;
            continue;
        }
        char spec = format[i + 1];
        if (spec == '%') {
            out += '%';
            i++;
        } else if (std::strchr("sdifoOc", spec) && next < args.size()) {
            const std::string& arg = args[next++];
            if (spec == 'd' || spec == 'i' || spec == 'f') {
                char* end = nullptr;
                double v = std::strtod(arg.c_str(), &end);
                if (end == arg.c_str()) out += "NaN";
                else if (spec == 'f') out += arg;
                else out += std::to_string(static_cast<long long>(v));
            } else if (spec != 'c') {  // %c takes a CSS string, which plain text can't show
                out += arg;
            }
            i++;
        } else {
            out += c;
        }
    }
    for (; next < args.size(); next++) out += " " + args[next];
    return out;
}
#endif

}  // namespace

void HostChromeClient::flush_console() {
    if (!pending_console_.active) return;
    pending_console_.active = false;
    if (events_.console)
        events_.console(pending_console_.message, pending_console_.line, pending_console_.source, pending_console_.level);
}

void HostChromeClient::addMessageToConsole(WKC::MessageSource source, WKC::MessageType, WKC::MessageLevel level,
                                           const WKC::String& message, unsigned int lineNumber,
                                           const WKC::String& sourceID) {
    flush_console();
#if NXB_CORE == 3
    if (source == WKC::MessageSource::ConsoleAPI) {
        pending_console_ = {true, to_std(message), to_std(sourceID), lineNumber, static_cast<int>(level)};
        RunLoop::main().post([this] { flush_console(); });
        return;
    }
#else
    (void)source;
#endif
    if (events_.console) events_.console(to_std(message), lineNumber, to_std(sourceID), static_cast<int>(level));
}

#if NXB_CORE == 3
void HostChromeClient::addMessageWithArgumentsToConsole(WKC::MessageSource, WKC::MessageLevel level,
                                                        const WKC::String& message,
                                                        const std::vector<WKC::String>& arguments,
                                                        unsigned int lineNumber, unsigned, const WKC::String& sourceID) {
    // The engine's std::vector is libc++'s; read its {begin, end, capacity} directly instead of
    // trusting libstdc++'s vector to match it.
    struct RawVector {
        const WKC::String *begin, *end, *cap;
    };
    auto& raw = reinterpret_cast<const RawVector&>(arguments);
    std::vector<std::string> args;
    for (auto* p = raw.begin; p != raw.end; ++p) args.push_back(to_std(*p));
    std::string first = to_std(message), source = to_std(sourceID);
    // Normally this completes the message addMessageToConsole just held back.
    if (pending_console_.active && pending_console_.message == first && pending_console_.line == lineNumber)
        pending_console_.active = false;
    else
        flush_console();
    if (events_.console) events_.console(format_console(first, args), lineNumber, source, static_cast<int>(level));
}
#endif

void HostChromeClient::runJavaScriptAlert(WKC::Frame*, const WKC::String& message) {
    if (events_.alert) events_.alert(to_std(message));
}

bool HostChromeClient::runJavaScriptConfirm(WKC::Frame*, const WKC::String& message) {
    LOG("confirm(%s) -> true", to_std(message).c_str());
    return true;
}

bool HostChromeClient::runJavaScriptPrompt(WKC::Frame*, const WKC::String& message, const WKC::String& defaultValue,
                                           WKC::String& result) {
    LOG("prompt(%s) -> default", to_std(message).c_str());
    result = defaultValue;
    return true;
}

WKC::String HostFrameLoaderClient::userAgent(const WKC::KURL&) {
    return WKC::String(config_.user_agent.c_str());
}

void HostFrameLoaderClient::dispatchDidFinishLoad() {
    if (events_.load_finished) events_.load_finished();
}

void HostFrameLoaderClient::dispatchDidFailLoad(const WKC::ResourceError& e) {
    if (events_.load_failed) events_.load_failed(to_std(e.failingURL()), to_std(e.localizedDescription()));
}

void HostFrameLoaderClient::dispatchDidFailProvisionalLoad(const WKC::ResourceError& e) {
    dispatchDidFailLoad(e);
}

void HostFrameLoaderClient::dispatchDidReceiveTitle(const WKC::String& title) {
    if (events_.title_changed) events_.title_changed(to_std(title));
}

namespace {

// Key event type names differ between the WKC versions.
#if NXB_CORE == 3
constexpr auto kKeyDown = WKC::EKeyEventDown;
constexpr auto kKeyChar = WKC::EKeyEventPress;
#else
constexpr auto kKeyDown = WKC::EKeyEventPressed;
constexpr auto kKeyChar = WKC::EKeyEventChar;
#endif

// WebKit editor command for an editing key (nullptr if the key isn't an editing key).
const char* editing_command(int key, int mods) {
    bool shift = mods & WKC::EModifierShift, ctrl = mods & WKC::EModifierCtrl;
    switch (key) {
        case WKC::EKeyBack: return ctrl ? "DeleteWordBackward" : "DeleteBackward";
        case WKC::EKeyDelete: return ctrl ? "DeleteWordForward" : "DeleteForward";
        case WKC::EKeyLeft:
            return ctrl ? (shift ? "MoveWordLeftAndModifySelection" : "MoveWordLeft")
                        : (shift ? "MoveLeftAndModifySelection" : "MoveLeft");
        case WKC::EKeyRight:
            return ctrl ? (shift ? "MoveWordRightAndModifySelection" : "MoveWordRight")
                        : (shift ? "MoveRightAndModifySelection" : "MoveRight");
        case WKC::EKeyUp: return shift ? "MoveUpAndModifySelection" : "MoveUp";
        case WKC::EKeyDown: return shift ? "MoveDownAndModifySelection" : "MoveDown";
        case WKC::EKeyHome:
            return shift ? "MoveToBeginningOfLineAndModifySelection" : "MoveToBeginningOfLine";
        case WKC::EKeyEnd: return shift ? "MoveToEndOfLineAndModifySelection" : "MoveToEndOfLine";
        case WKC::EKeyReturn: return shift ? "InsertLineBreak" : "InsertNewline";
        case WKC::EKeyA: return ctrl ? "SelectAll" : nullptr;
        case WKC::EKeyZ: return ctrl ? (shift ? "Redo" : "Undo") : nullptr;
        case WKC::EKeyY: return ctrl ? "Redo" : nullptr;
        default: return nullptr;
    }
}

std::string utf8(unsigned cp) {
    std::string s;
    if (cp < 0x80) s += static_cast<char>(cp);
    else if (cp < 0x800) s += static_cast<char>(0xC0 | (cp >> 6)), s += static_cast<char>(0x80 | (cp & 63));
    else if (cp < 0x10000)
        s += static_cast<char>(0xE0 | (cp >> 12)), s += static_cast<char>(0x80 | ((cp >> 6) & 63)),
            s += static_cast<char>(0x80 | (cp & 63));
    else
        s += static_cast<char>(0xF0 | (cp >> 18)), s += static_cast<char>(0x80 | ((cp >> 12) & 63)),
            s += static_cast<char>(0x80 | ((cp >> 6) & 63)), s += static_cast<char>(0x80 | (cp & 63));
    return s;
}

}  // namespace

void HostEditorClient::handleKeyboardEvent(WKC::KeyboardEvent* event) {
    if (!event) return;
    WKC::WKCKeyEvent k = event->keyEvent();
    WKC::EventTarget* target = event->target();
    WKC::Node* node = target ? target->toNode() : nullptr;
    WKC::Document* doc = node ? node->document() : nullptr;
    WKC::Frame* frame = doc ? doc->frame() : nullptr;
    WKC::Editor* editor = frame ? frame->editor() : nullptr;
    if (!editor) return;

    if (k.m_type == kKeyChar) {
        bool command_mod = k.m_modifiers & (WKC::EModifierCtrl | WKC::EModifierAlt);
        if (k.m_char >= 0x20 && k.m_char != 0x7F && !command_mod) {
            if (editor->insertText(WKC::String::fromUTF8(utf8(k.m_char).c_str()), event)) event->setDefaultHandled();
        }
        return;
    }
    if (k.m_type == kKeyDown) {
        if (const char* name = editing_command(k.m_key, k.m_modifiers)) {
            WKC::Editor::Command cmd = editor->command(WKC::String(name));
            if (cmd.execute(event)) event->setDefaultHandled();
        }
    }
}

}  // namespace nxb

namespace nxb {

std::u16string HostStorageArea::to_u16(const WKC::String& s) {
    const unsigned short* chars = s.charactersWithNullTermination();
    return chars ? std::u16string(reinterpret_cast<const char16_t*>(chars), s.length()) : std::u16string();
}

WKC::String HostStorageArea::to_wkc(const std::u16string& s) {
    return WKC::String(reinterpret_cast<const unsigned short*>(s.data()), static_cast<unsigned>(s.size()));
}

std::shared_ptr<HostStorageArea::Items> HostStorageArea::items_for(const WKC::String& origin, int type) {
    static std::map<std::pair<std::u16string, int>, std::shared_ptr<Items>> all;
    auto& items = all[{to_u16(origin), type}];
    if (!items) items = std::make_shared<Items>();
    return items;
}

WKC::String HostStorageArea::key(unsigned index) {
    if (index >= items_->size()) return WKC::String();
    auto it = items_->begin();
    std::advance(it, index);
    return to_wkc(it->first);
}

WKC::String HostStorageArea::item(const WKC::String& key) {
    auto it = items_->find(to_u16(key));
    return it == items_->end() ? WKC::String() : to_wkc(it->second);  // null String -> getItem() returns null
}

void HostStorageArea::setItem(WKC::Frame*, const WKC::String& key, const WKC::String& value, bool& quotaException) {
    std::u16string k = to_u16(key), v = to_u16(value);
    if (quota_) {
        size_t used = 0;
        for (auto& kv : *items_)
            if (kv.first != k) used += kv.first.size() + kv.second.size();
        if ((used + k.size() + v.size()) * 2 > quota_) {
            quotaException = true;
            return;
        }
    }
    quotaException = false;
    (*items_)[k] = std::move(v);
}

}  // namespace nxb

namespace nxb {

void HostBackForwardList::addItem(WKC::HistoryItem* item) {
    if (!item) return;
    // Drop the forward entries, then the oldest one if the list is full.
    for (size_t i = current_ + 1; i < items_.size(); i++) WKC::HistoryItem::destroy(items_[i]);
    items_.resize(current_ + 1);
    if (items_.size() >= kCapacity) {
        WKC::HistoryItem::destroy(items_.front());
        items_.erase(items_.begin());
    }
    items_.push_back(WKC::HistoryItem::create(item, true));  // `item` is a temporary wrapper
    current_ = static_cast<int>(items_.size()) - 1;
}

void HostBackForwardList::goToItem(WKC::HistoryItem* item) {
    for (size_t i = 0; i < items_.size(); i++)
        if (items_[i]->compare(item)) {
            current_ = static_cast<int>(i);
            return;
        }
    LOG("warning: goToItem for an entry not in the back/forward list");
}

WKC::HistoryItem* HostBackForwardList::itemAtIndex(int index) {
    long i = static_cast<long>(current_) + index;
    return current_ >= 0 && i >= 0 && i < static_cast<long>(items_.size()) ? items_[i] : nullptr;
}

void HostBackForwardList::close() {
    for (auto* item : items_) WKC::HistoryItem::destroy(item);
    items_.clear();
    current_ = -1;
}

}  // namespace nxb

namespace nxb {

void HostDropDownListClient::show(const WKCRect& r, WKC::FrameView* view, int index, WKC::PopupMenuClient* client) {
    // `r` is in the coordinates of the frame's document; `view` is only valid during this call.
    rect_ = view ? view->contentsToWindow(r) : r;
    active_ = client;
    if (!events_.popup_shown) {
        choose(-1);  // nothing can show it; don't leave the <select> stuck open
        return;
    }
    publish(index);
}

void HostDropDownListClient::hide(WKC::PopupMenuClient* client) {
    // The engine calls popupDidHide itself here; the client may already be half destroyed.
    if (!active_ || client != active_) return;
    active_ = nullptr;
    if (events_.popup_hidden) events_.popup_hidden();
}

void HostDropDownListClient::updateFromElement(WKC::PopupMenuClient* client) {
    if (client == active_ && active_) publish(client->selectedIndex());
}

void HostDropDownListClient::publish(int selected) {
    PopupMenu menu;
    menu.rect = rect_;
    menu.selected = selected;
    menu.font_size = active_->menuStyle_font_height();
    int n = active_->listSize();
    for (int i = 0; i < n; i++) {
        PopupMenu::Item item;
        if (active_->itemIsSeparator(i)) item.kind = PopupMenu::Item::Kind::Separator;
        else if (active_->itemIsLabel(i)) item.kind = PopupMenu::Item::Kind::Group;
        item.text = to_std(active_->itemText(i));
        item.enabled = active_->itemIsEnabled(i);
        menu.items.push_back(std::move(item));
    }
    events_.popup_shown(menu);
}

void HostDropDownListClient::choose(int index) {
    WKC::PopupMenuClient* client = active_;
    if (!client) return;
    active_ = nullptr;
    if (events_.popup_hidden) events_.popup_hidden();
    // Labels, separators and disabled options can't be chosen (the engine doesn't check).
    if (index >= client->listSize() || (index >= 0 && !client->itemIsEnabled(index))) index = -1;
    // The order of WebKit's own ports: popupDidHide, then valueChanged. valueChanged fires the
    // change event, whose handlers may remove the <select> and with it `client`.
    client->popupDidHide();
    if (index >= 0) client->valueChanged(static_cast<unsigned>(index), true);
}

}  // namespace nxb
