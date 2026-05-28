#pragma once

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <ctime>

#ifdef _MSC_VER
#  define ADE_CRASH_FOPEN(f, p, m) fopen_s(&(f), p, m)
#else
#  define ADE_CRASH_FOPEN(f, p, m) ((f) = std::fopen(p, m))
#endif

namespace compressor {
namespace ade {

inline void ade_debug_write(const char* msg) {
    static FILE* g_file = nullptr;
    if (!g_file) {
        const char* path = std::getenv("ADE_CRASH_LOG_PATH");
        if (path && path[0]) {
            ADE_CRASH_FOPEN(g_file, path, "a");
        }
        if (!g_file) return;
    }
    char time_buf[32];
    auto now = std::time(nullptr);
    std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", std::localtime(&now));
    std::fprintf(g_file, "%s [C++] %s\n", time_buf, msg);
    std::fflush(g_file);
}

inline void ade_debug_writef(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ade_debug_write(buf);
}

}  // namespace ade
}  // namespace compressor