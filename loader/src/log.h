#pragma once

#include <cstdio>
#include <cstdlib>

#define LOG(fmt, ...) std::fprintf(stderr, "[loader] " fmt "\n", ##__VA_ARGS__)
#define FATAL(fmt, ...)                                              \
    do {                                                             \
        std::fprintf(stderr, "[loader] FATAL: " fmt "\n", ##__VA_ARGS__); \
        std::abort();                                                \
    } while (0)

// Set NXB_TRACE=1 to log every shim call that opts into tracing.
bool trace_enabled();
#define TRACE(fmt, ...)                                                       \
    do {                                                                      \
        if (trace_enabled()) std::fprintf(stderr, "[trace] " fmt "\n", ##__VA_ARGS__); \
    } while (0)
