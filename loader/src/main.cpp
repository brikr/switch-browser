// wkc-loader: loads the Switch browser engine (NetFront / WKC) modules and runs them on Linux AArch64.

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "engine_imports.h"
#include "host/browser.h"
#include "host/run_loop.h"
#include "linker.h"
#include "log.h"

namespace {

// Dependencies first: webkit_wkc imports from all others; peer_wkc from oss/cairo/libfont.
const std::vector<std::string> kModules = {"oss_wkc", "cairo_wkc", "libfont", "peer_wkc", "webkit_wkc"};

// The applet builds the user agent itself (no UA text exists in the engine). core_3's default is a
// string observed from a real Switch (another seen: "... NF/7.0.3.12.11 NintendoBrowser/5.2.0.55481",
// probably a newer firmware); core_0's is an older-format approximation.
#if NXB_CORE == 3
const char* kDefaultUserAgent =
    "Mozilla/5.0 (Nintendo Switch; WebApplet) AppleWebKit/613.0 (KHTML, like Gecko) NF/6.0.3.31.11 "
    "NintendoBrowser/5.1.0.55481";
#else
const char* kDefaultUserAgent =
    "Mozilla/5.0 (Nintendo Switch; WebApplet) AppleWebKit/601.6 (KHTML, like Gecko) NF/4.0.0.5.10 "
    "NintendoBrowser/5.1.0.13343";
#endif

const char* kDefaultHtml =
    "<!doctype html><html><head><title>wkc-loader</title></head><body>"
    "<script>console.log('1+1 =', 1 + 1); console.log(navigator.userAgent);</script>"
    "</body></html>";

[[noreturn]] void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s --engine <dir> --romfs <dir> [--html <file> | --url <url>] [--eval <js>]...\n"
                 "  --engine      directory containing the decompressed engine NROs (%s.nro, ...)\n"
                 "  --romfs       BrowserDll RomFS root (served as rom:/)\n"
                 "  --html        load this HTML file (default: a small console.log test page)\n"
                 "  --url         load this URL instead\n"
                 "  --eval        evaluate JS in the page after it loads and print the result (repeatable)\n"
                 "  --user-agent  override the user agent\n"
                 "  --device-mode console|handheld (default console): the docked/handheld state seen by the\n"
                 "                -webkit-nintendo-switch-device-mode media feature\n"
                 "  --timeout     seconds to wait for the load (default 30)\n"
                 "  --fonts       directory of .ttf fonts to register (prepare_firmware.py's <out>/fonts)\n"
                 "  --screenshot  write the rendered page to this PNG file after loading\n"
                 "  --certs       directory of .pem root CAs to trust (prepare_firmware.py's <out>/certs)\n"
                 "env: NXB_DATA (writable mounts, default ./nxb-data), NXB_TRACE=1 (verbose)\n",
                 argv0, kModules[0].c_str());
    std::exit(2);
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) FATAL("cannot read %s", path.c_str());
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
    std::string engine_dir, romfs_dir, html_path, url, fonts_dir, screenshot_path, certs_dir;
    std::vector<std::string> evals;
    nxb::ViewConfig config;
    config.user_agent = kDefaultUserAgent;
    int timeout_s = 30;
    for (int i = 1; i < argc; i++) {
        auto arg = [&](const char* name) { return !std::strcmp(argv[i], name) && i + 1 < argc; };
        if (arg("--engine")) engine_dir = argv[++i];
        else if (arg("--romfs")) romfs_dir = argv[++i];
        else if (arg("--html")) html_path = argv[++i];
        else if (arg("--url")) url = argv[++i];
        else if (arg("--eval")) evals.push_back(argv[++i]);
        else if (arg("--user-agent")) config.user_agent = argv[++i];
        else if (arg("--device-mode")) {
            std::string m = argv[++i];
            if (m == "console") config.handheld = false;
            else if (m == "handheld") config.handheld = true;
            else usage(argv[0]);
        }
        else if (arg("--timeout")) timeout_s = std::atoi(argv[++i]);
        else if (arg("--fonts")) fonts_dir = argv[++i];
        else if (arg("--screenshot")) screenshot_path = argv[++i];
        else if (arg("--certs")) certs_dir = argv[++i];
        else usage(argv[0]);
    }
    if (engine_dir.empty() || romfs_dir.empty()) usage(argv[0]);
    setenv("NXB_ROMFS", romfs_dir.c_str(), 1);

    Linker& linker = Linker::instance();
    linker.load(engine_dir, kModules);
    linker.link();
    linker.run_init();
    bind_engine_imports(linker);

    nxb::Browser browser;
    bool loaded = false, failed = false;
    browser.events.console = [](const std::string& msg, unsigned line, const std::string& src, int level) {
        std::printf("console[%d] %s:%u: %s\n", level, src.empty() ? "(inline)" : src.c_str(), line, msg.c_str());
        std::fflush(stdout);
    };
    browser.events.alert = [](const std::string& msg) { std::printf("alert: %s\n", msg.c_str()); };
    browser.events.title_changed = [](const std::string& t) { LOG("title: %s", t.c_str()); };
    browser.events.load_finished = [&] { loaded = true; };
    browser.events.load_failed = [&](const std::string& u, const std::string& d) {
        LOG("load failed: %s (%s)", u.c_str(), d.c_str());
        failed = true;
    };

    if (!browser.initialize(size_t(256) << 20, size_t(1) << 20)) return 1;
    if (!fonts_dir.empty() && browser.register_fonts(fonts_dir) == 0) LOG("warning: no fonts registered from %s", fonts_dir.c_str());
    if (!certs_dir.empty() && browser.register_root_cas(certs_dir) == 0) LOG("warning: no root CAs registered from %s", certs_dir.c_str());
    if (!browser.create_view(config)) return 1;

    if (!url.empty()) browser.load_url(url);
    else browser.load_html(html_path.empty() ? kDefaultHtml : read_file(html_path), "about:blank");

    auto& loop = nxb::RunLoop::main();
    if (!loop.run_until([&] { return loaded || failed; }, std::chrono::seconds(timeout_s)))
        LOG("timed out waiting for the page to load");
    else
        LOG("page %s", loaded ? "loaded" : "failed");
    loop.run_until_idle(std::chrono::milliseconds(200), std::chrono::seconds(2));

    if (!screenshot_path.empty()) browser.screenshot(screenshot_path);

    for (auto& js : evals) {
        bool threw = false;
        std::string result = browser.evaluate(js, &threw);
        std::printf("%s %s => %s\n", threw ? "threw" : "eval", js.c_str(), result.c_str());
    }
    std::fflush(stdout);
    // Skip engine teardown for now; just exit.
    std::_Exit(failed ? 1 : 0);
}
