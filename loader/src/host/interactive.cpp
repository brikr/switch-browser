#include "host/interactive.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>

#include "host/run_loop.h"
#include "host/ws_server.h"
#include "log.h"
#include "stub_report.h"

namespace nxb {
namespace {

// --- tiny JSON helpers (flat objects with string/number/bool values) ---

using JsonObject = std::map<std::string, std::string>;  // values as raw strings

JsonObject parse_flat_json(const std::string& s) {
    JsonObject out;
    size_t i = 0;
    auto skip_ws = [&] {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    };
    auto parse_string = [&]() -> std::string {
        std::string v;
        i++;  // opening quote
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char e = s[++i];
                if (e == 'n') v += '\n';
                else if (e == 't') v += '\t';
                else if (e == 'r') v += '\r';
                else if (e == 'u' && i + 4 < s.size()) {
                    unsigned cp = std::stoul(s.substr(i + 1, 4), nullptr, 16);
                    i += 4;
                    // UTF-8 encode (surrogate pairs are combined below if present)
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') {
                        unsigned lo = std::stoul(s.substr(i + 3, 4), nullptr, 16);
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                    if (cp < 0x80) v += static_cast<char>(cp);
                    else if (cp < 0x800) v += static_cast<char>(0xC0 | (cp >> 6)), v += static_cast<char>(0x80 | (cp & 63));
                    else if (cp < 0x10000)
                        v += static_cast<char>(0xE0 | (cp >> 12)), v += static_cast<char>(0x80 | ((cp >> 6) & 63)),
                            v += static_cast<char>(0x80 | (cp & 63));
                    else
                        v += static_cast<char>(0xF0 | (cp >> 18)), v += static_cast<char>(0x80 | ((cp >> 12) & 63)),
                            v += static_cast<char>(0x80 | ((cp >> 6) & 63)), v += static_cast<char>(0x80 | (cp & 63));
                } else v += e;
                i++;
            } else {
                v += s[i++];
            }
        }
        i++;  // closing quote
        return v;
    };
    skip_ws();
    if (i >= s.size() || s[i] != '{') return out;
    i++;
    while (i < s.size()) {
        skip_ws();
        if (s[i] == '}') break;
        if (s[i] != '"') break;
        std::string key = parse_string();
        skip_ws();
        if (i >= s.size() || s[i] != ':') break;
        i++;
        skip_ws();
        std::string value;
        if (i < s.size() && s[i] == '"') {
            value = parse_string();
        } else {
            size_t start = i;
            while (i < s.size() && s[i] != ',' && s[i] != '}') i++;
            value = s.substr(start, i - start);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
        }
        out[key] = value;
        skip_ws();
        if (i < s.size() && s[i] == ',') i++;
    }
    return out;
}

std::string json_escape(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
        } else out += static_cast<char>(c);
    }
    return out + "\"";
}

int num(const JsonObject& o, const char* k, int def = 0) {
    auto it = o.find(k);
    return it == o.end() || it->second.empty() ? def : static_cast<int>(std::stod(it->second));
}
double numf(const JsonObject& o, const char* k) {
    auto it = o.find(k);
    return it == o.end() || it->second.empty() ? 0.0 : std::stod(it->second);
}
std::string str(const JsonObject& o, const char* k) {
    auto it = o.find(k);
    return it == o.end() ? "" : it->second;
}

// --- dirty-region tracking ---

struct Dirty {
    bool any = false;
    WKCRect r{};
    void add(const WKCRect& a) {
        if (a.fWidth <= 0 || a.fHeight <= 0) return;
        if (!any) {
            r = a, any = true;
            return;
        }
        int x0 = std::min(r.fX, a.fX), y0 = std::min(r.fY, a.fY);
        int x1 = std::max(r.fX + r.fWidth, a.fX + a.fWidth), y1 = std::max(r.fY + r.fHeight, a.fY + a.fHeight);
        r = WKCRect{x0, y0, x1 - x0, y1 - y0};
    }
};

}  // namespace

int run_interactive(Browser& browser, const ServeOptions& opts) {
    WsServer server;
    RunLoop& loop = RunLoop::main();
    Dirty dirty;  // touched only on the loop thread
    std::string title;
    bool loading = false;
    std::string popup;  // the open <select> list's message, resent to a client that (re)connects

    auto send_state = [&] {
        server.send_text("{\"t\":\"state\",\"url\":" + json_escape(browser.current_url()) + ",\"title\":" +
                         json_escape(title) + ",\"loading\":" + (loading ? "true" : "false") + ",\"width\":" +
                         std::to_string(browser.width()) + ",\"height\":" + std::to_string(browser.height()) +
                         ",\"core\":" + std::to_string(opts.core) + ",\"ua\":" + json_escape(opts.user_agent) + "}");
    };

    // The URL plus whether back/forward are possible (for the toolbar buttons).
    auto send_url = [&] {
        server.send_text("{\"t\":\"url\",\"v\":" + json_escape(browser.current_url()) +
                         ",\"back\":" + (browser.can_go_back_or_forward(-1) ? "true" : "false") +
                         ",\"fwd\":" + (browser.can_go_back_or_forward(1) ? "true" : "false") + "}");
    };

    browser.events.repaint = [&](const WKCRect& r) { dirty.add(r); };
    browser.events.title_changed = [&](const std::string& t) {
        title = t;
        server.send_text("{\"t\":\"title\",\"v\":" + json_escape(t) + "}");
    };
    // A new page (or same-document navigation) doesn't necessarily invalidate the whole view, so
    // repaint everything; otherwise the UI keeps pixels from the previous page.
    auto full_repaint = [&] { dirty.add(WKCRect{0, 0, browser.width(), browser.height()}); };
    browser.events.location_changed = [&] {
        full_repaint();
        send_url();
    };
    browser.events.load_started = [&] {
        loading = true;
        server.send_text("{\"t\":\"loading\",\"v\":true}");
    };
    browser.events.load_finished = [&] {
        full_repaint();
        loading = false;
        server.send_text("{\"t\":\"loading\",\"v\":false}");
        send_url();
    };
    browser.events.load_failed = [&](const std::string& url, const std::string& desc) {
        loading = false;
        server.send_text("{\"t\":\"loading\",\"v\":false}");
        server.send_text("{\"t\":\"console\",\"level\":3,\"msg\":" + json_escape("load failed: " + url + " (" + desc + ")") +
                         ",\"src\":\"\",\"line\":0}");
    };
    browser.events.console = [&](const std::string& msg, unsigned line, const std::string& src, int level) {
        server.send_text("{\"t\":\"console\",\"level\":" + std::to_string(level) + ",\"msg\":" + json_escape(msg) +
                         ",\"src\":" + json_escape(src) + ",\"line\":" + std::to_string(line) + "}");
    };
    browser.events.alert = [&](const std::string& msg) {
        server.send_text("{\"t\":\"alert\",\"msg\":" + json_escape(msg) + "}");
    };

    browser.events.popup_shown = [&](const PopupMenu& menu) {
        std::string items;
        for (auto& item : menu.items) {
            const char* kind = item.kind == PopupMenu::Item::Kind::Group ? "g"
                               : item.kind == PopupMenu::Item::Kind::Separator ? "s" : "o";
            items += std::string(items.empty() ? "" : ",") + "{\"k\":\"" + kind + "\",\"l\":" + json_escape(item.text) +
                     ",\"e\":" + (item.enabled ? "true" : "false") + "}";
        }
        const WKCRect& r = menu.rect;
        popup = "{\"t\":\"popup\",\"x\":" + std::to_string(r.fX) + ",\"y\":" + std::to_string(r.fY) +
                ",\"w\":" + std::to_string(r.fWidth) + ",\"h\":" + std::to_string(r.fHeight) +
                ",\"sel\":" + std::to_string(menu.selected) + ",\"fs\":" + std::to_string(menu.font_size) +
                ",\"items\":[" + items + "]}";
        server.send_text(popup);
    };
    browser.events.popup_hidden = [&] {
        popup.clear();
        server.send_text("{\"t\":\"popup_hide\"}");
    };

    // Unimplemented host features the page touched; shown as warnings in the UI console.
    auto send_stub = [&server](const std::string& name) {
        server.send_text("{\"t\":\"stub\",\"name\":" + json_escape(name) + "}");
    };
    set_stub_listener([send_stub](const char* name) { send_stub(name); });

    // Commands arrive on the server thread; run them on the loop thread.
    server.on_message = [&](const std::string& text) {
        JsonObject m = parse_flat_json(text);
        loop.post([&browser, &server, &dirty, m] {
            const std::string t = str(m, "t");
            if (t == "nav") {
                std::string url = str(m, "url");
                if (url.find("://") == std::string::npos && url.rfind("about:", 0) != 0 && url.rfind("data:", 0) != 0)
                    url = "https://" + url;
                browser.load_url(url);
            } else if (t == "back") {
                browser.go_back_or_forward(-1);
            } else if (t == "fwd") {
                browser.go_back_or_forward(1);
            } else if (t == "reload") {
                browser.reload();
            } else if (t == "stop") {
                browser.stop();
            } else if (t == "mouse") {
                std::string k = str(m, "k");
                auto kind = k == "down" ? Browser::MouseKind::Down
                            : k == "up" ? Browser::MouseKind::Up
                            : k == "dbl" ? Browser::MouseKind::DoubleClick : Browser::MouseKind::Move;
                browser.mouse(kind, num(m, "x"), num(m, "y"), num(m, "b"), num(m, "m"));
            } else if (t == "wheel") {
                browser.wheel(num(m, "x"), num(m, "y"), static_cast<float>(numf(m, "dx")),
                              static_cast<float>(numf(m, "dy")), num(m, "m"));
            } else if (t == "key") {
                browser.key(str(m, "k") == "down", num(m, "code"), num(m, "m"), str(m, "r") == "true");
            } else if (t == "text") {
                browser.text(static_cast<unsigned>(num(m, "c")));
            } else if (t == "mode") {
                browser.set_device_mode(str(m, "handheld") == "true");
            } else if (t == "eval") {
                bool threw = false;
                std::string js = str(m, "js");
                std::string result = browser.evaluate(js, &threw);
                server.send_text("{\"t\":\"eval\",\"js\":" + json_escape(js) + ",\"result\":" + json_escape(result) +
                                 ",\"threw\":" + (threw ? "true" : "false") + "}");
            } else if (t == "popup") {
                browser.popup_choose(num(m, "i", -1));
            } else if (t == "refresh") {
                dirty.add(WKCRect{0, 0, browser.width(), browser.height()});
            }
        });
        loop.wake();
    };
    server.on_connect = [&] {
        loop.post([&] {
            send_state();
            send_url();
            if (!popup.empty()) server.send_text(popup);
            for (auto& s : stub_counts())  // ones hit before this client connected
                if (!s.expected) send_stub(s.name);
            dirty.add(WKCRect{0, 0, browser.width(), browser.height()});
        });
        loop.wake();
    };

    if (!server.start(opts.bind, opts.port, opts.ui_dir)) return 1;
    LOG("UI: open http://%s:%d/ in a browser", opts.bind == "0.0.0.0" ? "localhost" : opts.bind.c_str(), opts.port);

    // Push repainted regions at most ~30 times a second.
    auto last_frame = std::chrono::steady_clock::now();
    loop.on_iteration = [&] {
        auto now = std::chrono::steady_clock::now();
        if (!dirty.any || !server.connected() || now - last_frame < std::chrono::milliseconds(33)) return;
        last_frame = now;
        WKCRect r = dirty.r;
        dirty.any = false;
        std::vector<uint8_t> pixels = browser.paint_rgba(r);
        if (pixels.empty()) return;
        std::vector<uint8_t> msg(16 + pixels.size());
        uint32_t hdr[4] = {static_cast<uint32_t>(r.fX), static_cast<uint32_t>(r.fY), static_cast<uint32_t>(r.fWidth),
                           static_cast<uint32_t>(r.fHeight)};
        std::memcpy(msg.data(), hdr, 16);
        std::memcpy(msg.data() + 16, pixels.data(), pixels.size());
        server.send_binary(msg.data(), msg.size());
    };

    if (!opts.start_url.empty()) browser.load_url(opts.start_url);
    for (;;) loop.run_until([] { return false; }, std::chrono::hours(24));
}

}  // namespace nxb
