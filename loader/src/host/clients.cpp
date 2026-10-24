#include "host/clients.h"

#include <helpers/WKCDocument.h>
#include <helpers/WKCEditor.h>
#include <helpers/WKCEventTarget.h>
#include <helpers/WKCFrame.h>
#include <helpers/WKCKeyboardEvent.h>
#include <helpers/WKCNode.h>
#include <helpers/WKCResourceError.h>

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

void HostChromeClient::addMessageToConsole(WKC::MessageSource, WKC::MessageType, WKC::MessageLevel level,
                                           const WKC::String& message, unsigned int lineNumber,
                                           const WKC::String& sourceID) {
    if (events_.console) events_.console(to_std(message), lineNumber, to_std(sourceID), static_cast<int>(level));
}

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
