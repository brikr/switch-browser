#pragma once

#include "host/wkc_api.h"

namespace nxb {

// Registers host file-system callbacks with the engine.
// Call after WKCWebKitInitialize.
void install_file_procs();

}  // namespace nxb
