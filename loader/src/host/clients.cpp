#include "host/clients.h"

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
WKCFloatRect HostChromeClient::pageRect() { return windowRect(); }

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

WKC::String HostFrameLoaderClient::userAgent(const WKC::KURL&) { return WKC::String(config_.user_agent.c_str()); }

void HostFrameLoaderClient::dispatchDidFinishLoad() {
    if (events_.load_finished) events_.load_finished();
}

void HostFrameLoaderClient::dispatchDidFailLoad(const WKC::ResourceError& e) {
    if (events_.load_failed) events_.load_failed(to_std(e.failingURL()), to_std(e.localizedDescription()));
}

void HostFrameLoaderClient::dispatchDidFailProvisionalLoad(const WKC::ResourceError& e) { dispatchDidFailLoad(e); }

void HostFrameLoaderClient::dispatchDidReceiveTitle(const WKC::String& title) {
    if (events_.title_changed) events_.title_changed(to_std(title));
}

}  // namespace nxb
