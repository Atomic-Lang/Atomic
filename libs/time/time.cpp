// time.cpp
// Time library for Atomic — cross-platform (Windows/Linux), dynamic linking (.dla)
//
// Build (.dla):
//   Windows: g++ -std=c++17 -shared -o libs/time/time.dla libs/time/time.cpp -lkernel32
//   Linux:   g++ -std=c++17 -shared -fPIC -o libs/time/time.dla libs/time/time.cpp

#include <cstdint>
#include <cstring>
#include <ctime>

// =============================================================================
// EXPORT MACRO
// =============================================================================

#ifdef _WIN32
    #define AT_EXPORT extern "C" __declspec(dllexport)
#else
    #define AT_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// =============================================================================
// PLATFORM DECLARATIONS
// =============================================================================

#ifdef _WIN32

extern "C" {
    typedef long long time64_t;
    time64_t _time64(time64_t*);
    struct tm* _localtime64(const time64_t*);
    void __stdcall Sleep(unsigned long dwMilliseconds);
    int __stdcall QueryPerformanceCounter(int64_t* lpPerformanceCount);
    int __stdcall QueryPerformanceFrequency(int64_t* lpFrequency);
}

#else

#include <unistd.h>

#endif

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

static char str_buffer[256];

struct TmLocal {
    int sec;
    int min;
    int hour;
    int mday;
    int mon;
    int year;
    int wday;
    int yday;
};

static TmLocal get_time() {
    TmLocal r;

#ifdef _WIN32
    time64_t now;
    _time64(&now);
    struct tm* t = _localtime64(&now);
#else
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
#endif

    r.sec  = t->tm_sec;
    r.min  = t->tm_min;
    r.hour = t->tm_hour;
    r.mday = t->tm_mday;
    r.mon  = t->tm_mon;
    r.year = t->tm_year;
    r.wday = t->tm_wday;
    r.yday = t->tm_yday;
    return r;
}

static const char* format_time(const char* fmt) {
#ifdef _WIN32
    time64_t now;
    _time64(&now);
    struct tm* t = _localtime64(&now);
#else
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
#endif

    strftime(str_buffer, sizeof(str_buffer), fmt, t);
    return str_buffer;
}

// Stopwatch
static int64_t sw_start = 0;

#ifdef _WIN32
static int64_t sw_freq = 0;
#endif

// =============================================================================
// EXPORTED FUNCTIONS — INT RETURN
// =============================================================================

AT_EXPORT int64_t tm_day() {
    return get_time().mday;
}

AT_EXPORT int64_t tm_month() {
    return get_time().mon + 1;
}

AT_EXPORT int64_t tm_year() {
    return get_time().year + 1900;
}

AT_EXPORT int64_t tm_hour() {
    return get_time().hour;
}

AT_EXPORT int64_t tm_minute() {
    return get_time().min;
}

AT_EXPORT int64_t tm_second() {
    return get_time().sec;
}

AT_EXPORT int64_t tm_weekday() {
    return get_time().wday;
}

AT_EXPORT int64_t tm_yearday() {
    return get_time().yday;
}

AT_EXPORT int64_t tm_timestamp() {
#ifdef _WIN32
    time64_t now;
    _time64(&now);
    return (int64_t)now;
#else
    return (int64_t)time(nullptr);
#endif
}

AT_EXPORT int64_t tm_date_num() {
    auto t = get_time();
    return (t.mday * 1000000) + ((t.mon + 1) * 10000) + (t.year + 1900);
}

AT_EXPORT int64_t tm_time_num() {
    auto t = get_time();
    return (t.hour * 10000) + (t.min * 100) + t.sec;
}

// =============================================================================
// EXPORTED FUNCTIONS — STRING RETURN
// =============================================================================

AT_EXPORT const char* tm_date_str() {
    return format_time("%d/%m/%Y");
}

AT_EXPORT const char* tm_time_str() {
    return format_time("%H:%M:%S");
}

AT_EXPORT const char* tm_full() {
    return format_time("%d/%m/%Y %H:%M:%S");
}

// =============================================================================
// EXPORTED FUNCTIONS — UTILITIES
// =============================================================================

AT_EXPORT int64_t tm_sleep(int64_t ms) {
#ifdef _WIN32
    Sleep((unsigned long)ms);
#else
    usleep((useconds_t)(ms * 1000));
#endif
    return 0;
}

AT_EXPORT int64_t tm_start() {
#ifdef _WIN32
    if (sw_freq == 0) {
        QueryPerformanceFrequency(&sw_freq);
    }
    QueryPerformanceCounter(&sw_start);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    sw_start = ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
    return 0;
}

AT_EXPORT int64_t tm_elapsed() {
#ifdef _WIN32
    int64_t now;
    QueryPerformanceCounter(&now);
    if (sw_freq == 0) return 0;
    return ((now - sw_start) * 1000) / sw_freq;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t now = ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return (now - sw_start) / 1000000;
#endif
}