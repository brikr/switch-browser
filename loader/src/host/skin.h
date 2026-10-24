#pragma once

#include <string>

namespace nxb {

// Registers the browser's skin from BrowserDll's RomFS (browser/skin/setting.json + image.bin,
// browser/DefaultCss.dat): theme colours (CSS system colours such as LinkText and ButtonText), form
// control images and system font sizes. Without a skin every system colour is transparent, so
// unstyled links and buttons draw invisible text. core_3 only (core_0's WKCSkin layout differs).
// Must be called before the view is created.
bool register_skin(const std::string& romfs_dir);

// The applet's user style sheet (browser/UserCss.dat on core_3, UserCssCore0.dat on core_0): a
// data: URL setting the default text colour, line height, scrollbar and tap-highlight styles.
// Returns an empty string if the file is missing.
std::string user_style_sheet_url(const std::string& romfs_dir);

}  // namespace nxb
