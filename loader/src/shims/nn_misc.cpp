// Remaining small nn:: imports: ro, time, util.

#include <sys/random.h>

#include <cstdarg>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "../host_symbols.h"

namespace {

using Result = uint32_t;

// The loader maps modules RWX and doesn't make RELRO read-only (yet).
void ProtectRelro(const void*, const void*, const void*, const void*, const void*) {}

struct CalendarTime {
    int16_t year;
    int8_t month, day, hour, minute, second;
};
struct TimeZone {
    char standard_time_name[8];
    bool is_daylight_saving_time;
    int32_t utc_offset_seconds;
};
struct CalendarAdditionalInfo {
    int32_t day_of_week;
    int32_t day_of_year;
    TimeZone time_zone;
};
struct PosixTime { int64_t value; };

Result ToCalendarTime(CalendarTime* out, CalendarAdditionalInfo* info, const PosixTime& t) {
    time_t secs = static_cast<time_t>(t.value);
    struct tm tm;
    localtime_r(&secs, &tm);
    out->year = static_cast<int16_t>(tm.tm_year + 1900);
    out->month = static_cast<int8_t>(tm.tm_mon + 1);
    out->day = static_cast<int8_t>(tm.tm_mday);
    out->hour = static_cast<int8_t>(tm.tm_hour);
    out->minute = static_cast<int8_t>(tm.tm_min);
    out->second = static_cast<int8_t>(tm.tm_sec);
    if (info) {
        info->day_of_week = tm.tm_wday;
        info->day_of_year = tm.tm_yday;
        std::memset(&info->time_zone, 0, sizeof info->time_zone);
        std::strncpy(info->time_zone.standard_time_name, tm.tm_zone ? tm.tm_zone : "UTC",
                     sizeof info->time_zone.standard_time_name - 1);
        // Report the full current offset with DST cleared: the engine's time peer snapshots this once
        // and subtracts extra hours when the DST flag is set (measured: offset - 7200 for PDT/BST).
        info->time_zone.is_daylight_saving_time = false;
        info->time_zone.utc_offset_seconds = static_cast<int32_t>(tm.tm_gmtoff);
    }
    return 0;
}

int SNPrintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

// --- diag ---

int GetBacktrace(uintptr_t*, int) { return 0; }

[[noreturn]] void AbortImpl(const char* condition, const char* function, const char* file, int line, const void*,
                            const char* fmt, ...) {
    std::fprintf(stderr, "[engine] nn::diag abort: %s in %s at %s:%d", condition ? condition : "?",
                 function ? function : "?", file ? file : "?", line);
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        std::fputs(": ", stderr);
        std::vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
    std::fputc('\n', stderr);
    std::abort();
}

// --- time zone name, crypto random ---

struct LocationName { char value[36]; };

void GetDeviceLocationName(LocationName* out) {
    const char* tz = std::getenv("NXB_TIMEZONE");
    std::memset(out->value, 0, sizeof out->value);
    std::strncpy(out->value, tz && *tz ? tz : "UTC", sizeof out->value - 1);
}

void GenerateCryptographicallyRandomBytes(void* buf, size_t size) {
    auto* p = static_cast<uint8_t*>(buf);
    while (size) {
        ssize_t n = getrandom(p, size, 0);
        if (n <= 0) continue;
        p += n;
        size -= static_cast<size_t>(n);
    }
}

HostSymbols symbols({
    {"_ZN2nn4diag12GetBacktraceEPmi", (void*)&GetBacktrace},
    {"_ZN2nn4diag6detail9AbortImplEPKcS3_S3_iPKNS_6ResultES3_z", (void*)&AbortImpl},
    {"_ZN2nn4time21GetDeviceLocationNameEPNS0_12LocationNameE", (void*)&GetDeviceLocationName},
    {"_ZN2nn6crypto36GenerateCryptographicallyRandomBytesEPvm", (void*)&GenerateCryptographicallyRandomBytes},
    {"_ZN2nn2ro12ProtectRelroEPKvS2_S2_S2_S2_", (void*)&ProtectRelro},
    {"_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeE",
     (void*)&ToCalendarTime},
    {"_ZN2nn4util8SNPrintfEPcmPKcz", (void*)&SNPrintf},
});

}  // namespace
