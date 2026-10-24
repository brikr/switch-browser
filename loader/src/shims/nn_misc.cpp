// Remaining small nn:: imports: ro, time, util.

#include <cstdarg>
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
        info->time_zone.is_daylight_saving_time = tm.tm_isdst > 0;
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

HostSymbols symbols({
    {"_ZN2nn2ro12ProtectRelroEPKvS2_S2_S2_S2_", (void*)&ProtectRelro},
    {"_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeE",
     (void*)&ToCalendarTime},
    {"_ZN2nn4util8SNPrintfEPcmPKcz", (void*)&SNPrintf},
});

}  // namespace
