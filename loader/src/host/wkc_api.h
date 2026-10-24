// The WKC (NetFront / WebKit) embedding API, from the WKC 0.30.0 headers in Nintendo's OSS
// release. Calls into it are resolved at runtime (see engine_imports).
#pragma once

#include <cstddef>  // some WKC headers use size_t without including it

#include <wkc/wkcbase.h>

// Match the engine build: cairo backend on; CustomJS off. The borrowed 3.30.3 wkcconfig.h (the
// 0.30.0 one is missing from the OSS package) enables CustomJS, but core_0's peer layer has no
// wkcCustomJS* functions. This changes FrameLoaderClientIf's vtable, so it must be right.
#ifndef USE_WKC_CAIRO
#define USE_WKC_CAIRO 1
#endif
#undef WKC_ENABLE_CUSTOMJS

#include <helpers/WKCHelpersEnums.h>
#include <helpers/WKCResourceError.h>
#include <helpers/WKCString.h>

#include <helpers/BackForwardClientIf.h>
#if NXB_CORE == 0
#include <helpers/BatteryClientIf.h>
#include <helpers/NavigatorContentUtilsClientIf.h>
#endif
#include <helpers/ChromeClientIf.h>
#include <helpers/ContextMenuClientIf.h>
#include <helpers/DeviceMotionClientIf.h>
#include <helpers/DeviceOrientationClientIf.h>
#include <helpers/DragClientIf.h>
#include <helpers/DropDownListClientIf.h>
#include <helpers/EditorClientIf.h>
#include <helpers/FrameLoaderClientIf.h>
#include <helpers/GeolocationClientIf.h>
#include <helpers/InspectorClientIf.h>
#include <helpers/ProgressTrackerClientIf.h>
#include <helpers/StorageAreaIf.h>
#include <helpers/VibrationClientIf.h>
#include <helpers/WebNfcClientIf.h>

#include <webkit/WKCClientBuilders.h>
#include <webkit/WKCMemoryEventHandler.h>
#include <webkit/WKCPrefs.h>
#if NXB_CORE == 0
#include <webkit/WKCTimerEventHandler.h>
#endif
#include <webkit/WKCWebFrame.h>
#include <webkit/WKCWebView.h>
