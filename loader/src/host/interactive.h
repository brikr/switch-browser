#pragma once

// Interactive mode (--serve): drives the engine from the browser UI in frontend/.
//
// Protocol over one WebSocket:
//  client -> host, JSON text: {"t":"nav","url"} {"t":"back"} {"t":"fwd"} {"t":"reload"} {"t":"stop"}
//    {"t":"mouse","k":"down|up|move|dbl","x","y","b","m"} {"t":"wheel","x","y","dx","dy","m"}
//    {"t":"key","k":"down|up","code","m","r"} {"t":"text","c"} {"t":"mode","handheld"}
//    {"t":"eval","js"} {"t":"refresh"}
//  host -> client, JSON text: {"t":"state",...} {"t":"title","v"} {"t":"url","v"} {"t":"loading","v"}
//    {"t":"console","level","msg","src","line"} {"t":"eval","js","result","threw"} {"t":"alert","msg"}
//  host -> client, binary: u32le x, y, w, h, then w*h RGBA8 pixels (a repainted region).

#include <string>

#include "host/browser.h"

namespace nxb {

struct ServeOptions {
    int port = 8000;
    std::string bind = "127.0.0.1";
    std::string ui_dir;
    std::string start_url;
    std::string user_agent;
    int core = 3;
};

// Runs forever (until the process is killed).
int run_interactive(Browser& browser, const ServeOptions& opts);

}  // namespace nxb
