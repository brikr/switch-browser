#include "host/skin.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <vector>

#include "host/wkc_api.h"
#include "log.h"

namespace nxb {
namespace {

bool read_all(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

#if NXB_CORE == 3

// --- minimal JSON reader, enough for setting.json (objects, arrays, numbers, strings, null) ---

struct Json {
    enum Kind { Null, Number, String, Array, Object } kind = Null;
    double num = 0;
    std::string str;
    std::vector<Json> items;                           // Array
    std::vector<std::pair<std::string, Json>> fields;  // Object, in file order

    const Json* get(const char* key) const {
        for (auto& f : fields)
            if (f.first == key) return &f.second;
        return nullptr;
    }
    int at(size_t i) const { return i < items.size() ? static_cast<int>(items[i].num) : 0; }
};

struct JsonParser {
    const std::string& s;
    size_t i = 0;
    bool ok = true;

    void ws() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    }
    std::string string() {
        std::string v;
        i++;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) i++;  // names here are plain ASCII
            v += s[i++];
        }
        i++;
        return v;
    }
    Json value() {
        Json v;
        ws();
        if (i >= s.size()) return ok = false, v;
        char c = s[i];
        if (c == '{') {
            v.kind = Json::Object;
            i++;
            for (ws(); ok && i < s.size() && s[i] != '}'; ws()) {
                if (s[i] != '"') return ok = false, v;
                std::string key = string();
                ws();
                if (i >= s.size() || s[i++] != ':') return ok = false, v;
                v.fields.emplace_back(key, value());
                ws();
                if (i < s.size() && s[i] == ',') i++;
            }
            i++;
        } else if (c == '[') {
            v.kind = Json::Array;
            i++;
            for (ws(); ok && i < s.size() && s[i] != ']'; ws()) {
                v.items.push_back(value());
                ws();
                if (i < s.size() && s[i] == ',') i++;
            }
            i++;
        } else if (c == '"') {
            v.kind = Json::String;
            v.str = string();
        } else if (s.compare(i, 4, "null") == 0) {
            i += 4;
        } else {
            char* end = nullptr;
            v.kind = Json::Number;
            v.num = std::strtod(s.c_str() + i, &end);
            if (end == s.c_str() + i) return ok = false, v;
            i = end - s.c_str();
        }
        return v;
    }
};

// Enum order of WKCSkin.h (3.30.3), without the ESkinImage/ESkinColor/ESystemFontType prefixes.
// setting.json uses the same names.
const char* const kImageNames[WKC::ESkinImages] = {
    "CheckboxUnchecked", "CheckboxChecked", "CheckboxUncheckedDisabled", "CheckboxCheckedDisabled",
    "CheckboxPressed", "CheckboxUncheckedFocused", "CheckboxCheckedFocused", "RadioUnchecked", "RadioChecked",
    "RadioUncheckedDisabled", "RadioCheckedDisabled", "RadioPressed", "RadioUncheckedFocused",
    "RadioCheckedFocused", "Button", "ButtonDisabled", "ButtonPressed", "ButtonHovered", "MenuListButton",
    "MenuListButtonDisabled", "MenuListButtonPressed", "MenuListButtonFocused", "VScrollbarBackground",
    "VScrollbarBackgroundDisabled", "VScrollbarThumb", "VScrollbarThumbHovered", "VScrollbarUp",
    "VScrollbarUpDisabled", "VScrollbarUpHovered", "VScrollbarDown", "VScrollbarDownDisabled",
    "VScrollbarDownHovered", "HScrollbarBackground", "HScrollbarBackgroundDisabled", "HScrollbarThumb",
    "HScrollbarThumbHovered", "HScrollbarLeft", "HScrollbarLeftDisabled", "HScrollbarLeftHovered",
    "HScrollbarRight", "HScrollbarRightDisabled", "HScrollbarRightHovered", "ScrollbarCrossCorner",
    "ScrollbarCrossCornerDisabled", "VRange", "VRangeDisabled", "VRangePressed", "VRangeHovered", "HRange",
    "HRangeDisabled", "HRangePressed", "HRangeHovered", "SearchFieldCancelButton", "SearchFieldResultButton",
};
const char* const kColorNames[WKC::ESkinColors] = {
    "ActiveBorder", "ActiveCaption", "ActiveText", "AppWorkSpace", "Background", "ButtonFace", "ButtonHighlight",
    "ButtonShadow", "ButtonText", "Canvas", "CanvasText", "CaptionText", "Field", "FieldText", "GrayText",
    "Highlight", "HighlightText", "InactiveBorder", "InactiveCaption", "InactiveCaptionText", "InfoBackground",
    "InfoText", "LinkText", "Menu", "MenuText", "Scrollbar", "Text", "ThreeDDarkShadow", "ThreeDFace",
    "ThreeDHighlight", "ThreeDLightShadow", "ThreeDShadow", "VisitedText", "Window", "WindowFrame", "WindowText",
    "ActiveSelectionForeground", "ActiveSelectionBackground", "InactiveSelectionForeground",
    "InactiveSelectionBackground", "FocusRing", "TextfieldBorder", "TextfieldBackground", "ProgressbarBorder",
    "ProgressbarBody", "ProgressbarBackground", "ProgressbarBackgroundDisabled", "RangeBorder",
    "RangeBackgroundDisabled",
};
const char* const kFontNames[WKC::ESystemFontTypes] = {
    "Caption", "Icon", "Menu", "MessageBox", "SmallCaption", "WebkitMiniControl", "WebkitSmallControl",
    "WebkitControl", "StatusBar",
};

// registerSkin copies the struct and strings but keeps the bitmap pointers, so these live forever.
std::string g_images;
std::string g_default_css;
std::vector<std::string> g_resource_names;
std::vector<WKC::WKCSkinResourceImage> g_resources;
std::vector<WKC::WKCSkinResourceImage*> g_resource_ptrs;

// Looks up a bitmap of `w` x `h` ARGB pixels at `offset` in image.bin. The bytes are B,G,R,A
// (native-endian 0xAARRGGBB words, as cairo uses), passed through unchanged.
void* image_at(const Json& e, int& w, int& h) {
    const Json* size = e.get("size");
    const Json* off = e.get("offset");
    if (!size || !off) return nullptr;
    w = size->at(0), h = size->at(1);
    size_t start = static_cast<size_t>(off->num), bytes = static_cast<size_t>(w) * h * 4;
    if (w <= 0 || h <= 0 || start + bytes > g_images.size()) return nullptr;
    return &g_images[start];
}

#endif

}  // namespace

bool register_skin(const std::string& romfs_dir) {
#if NXB_CORE == 3
    const std::string dir = romfs_dir + "/browser/skin";
    std::string text;
    if (!read_all(dir + "/setting.json", text) || !read_all(dir + "/image.bin", g_images)) {
        LOG("warning: no skin in %s; system colours (link and button text) will be transparent", dir.c_str());
        return false;
    }
    JsonParser parser{text};
    Json root = parser.value();
    if (!parser.ok || root.kind != Json::Object) {
        LOG("warning: cannot parse %s/setting.json", dir.c_str());
        return false;
    }

    static WKC::WKCSkin skin;  // ~3 KB
    std::memset(&skin, 0, sizeof skin);
    int images = 0, colors = 0;
    if (const Json* list = root.get("images")) {
        for (int i = 0; i < WKC::ESkinImages; i++) {
            const Json* e = list->get(kImageNames[i]);
            int w = 0, h = 0;
            void* bitmap = e ? image_at(*e, w, h) : nullptr;
            if (!bitmap) continue;
            WKC::WKCSkinImage& img = skin.fImages[i];
            img.fSize = WKCSize{w, h};
            img.fBitmap = bitmap;
            if (const Json* coords = e->get("coords"))
                for (size_t p = 0; p < 4 && p < coords->items.size(); p++)
                    img.fPoints[p] = WKCPoint{coords->items[p].at(0), coords->items[p].at(1)};
            images++;
        }
    }
    if (const Json* list = root.get("colors")) {
        for (int i = 0; i < WKC::ESkinColors; i++) {
            const Json* c = list->get(kColorNames[i]);
            if (!c) continue;
            // [r, g, b, a] -> 0xAARRGGBB
            skin.fColors[i] = static_cast<unsigned>(c->at(3)) << 24 | static_cast<unsigned>(c->at(0)) << 16 |
                              static_cast<unsigned>(c->at(1)) << 8 | static_cast<unsigned>(c->at(2));
            colors++;
        }
    }
    if (const Json* list = root.get("fontSizes"))
        for (int i = 0; i < WKC::ESystemFontTypes; i++)
            if (const Json* f = list->get(kFontNames[i])) skin.fSystemFontSize[i] = static_cast<float>(f->num);
    // The skin has no dark variant; the browser doesn't offer a dark mode.
    std::memcpy(skin.fDarkImages, skin.fImages, sizeof skin.fImages);
    std::memcpy(skin.fDarkColors, skin.fColors, sizeof skin.fColors);
    skin.fImageScale = 1.0f;

    if (const Json* list = root.get("resourceImages")) {
        g_resource_names.reserve(list->items.size());
        g_resources.reserve(list->items.size());
        for (auto& e : list->items) {
            const Json* name = e.get("name");
            int w = 0, h = 0;
            void* bitmap = image_at(e, w, h);
            if (!name || !bitmap) continue;
            g_resource_names.push_back(name->str);
            g_resources.push_back(WKC::WKCSkinResourceImage{g_resource_names.back().c_str(), WKCSize{w, h}, bitmap});
        }
        for (auto& r : g_resources) g_resource_ptrs.push_back(&r);
        g_resource_ptrs.push_back(nullptr);
        skin.fResourceImages = g_resource_ptrs.data();
    }
    if (read_all(romfs_dir + "/browser/DefaultCss.dat", g_default_css)) skin.fDefaultStyleSheet = g_default_css.c_str();

    WKC::WKCPrefs::registerSkin(&skin);
    LOG("skin: %d images, %d colours, %zu resource images", images, colors, g_resources.size());
    return true;
#else
    (void)romfs_dir;
    return false;
#endif
}

std::string user_style_sheet_url(const std::string& romfs_dir) {
#if NXB_CORE == 3
    const char* name = "UserCss.dat";
#else
    const char* name = "UserCssCore0.dat";
#endif
    std::string url;
    if (!read_all(romfs_dir + "/browser/" + name, url)) return "";
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.back()))) url.pop_back();
    return url;
}

}  // namespace nxb
