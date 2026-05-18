#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace compressor::debug {

/**
 * @brief DebugLog — 轻量级调试日志工具。
 *
 * 用法：
 *   DebugLog::instance().enable("debug.log");
 *   DEBUG_LOG("compress: input=%zu output=%zu", in_size, out_size);
 *
 * 通过 DEBUG_LOG_ENABLED 宏控制编译时开关。
 * 定义 DEBUG_LOG_ENABLED=1 启用，否则所有日志调用为空操作。
 */
class DebugLog {
   public:
    static DebugLog& instance() {
        static DebugLog inst;
        return inst;
    }

    void enable(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) file_.close();
        file_.open(path, std::ios::out | std::ios::trunc);
        enabled_ = file_.is_open();
        if (enabled_) {
            auto now = std::time(nullptr);
            file_ << "=== Debug Log started at " << std::ctime(&now) << "===\n";
            file_.flush();
        }
    }

    void disable() {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = false;
        if (file_.is_open()) file_.close();
    }

    bool is_enabled() const { return enabled_; }

    void log(const char* fmt, ...) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        va_list args;
        va_start(args, fmt);
        char buf[4096];
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        file_ << buf << "\n";
        file_.flush();
    }

   private:
    DebugLog() = default;
    ~DebugLog() { disable(); }
    DebugLog(const DebugLog&) = delete;
    DebugLog& operator=(const DebugLog&) = delete;

    bool enabled_{false};
    std::ofstream file_;
    std::mutex mutex_;
};

}  // namespace compressor::debug

// 编译时开关：定义 DEBUG_LOG_ENABLED=1 启用日志（默认关闭，零开销）
#if !defined(DEBUG_LOG_ENABLED)
#define DEBUG_LOG_ENABLED 0
#endif

#if DEBUG_LOG_ENABLED
#define DEBUG_LOG(...) \
    compressor::debug::DebugLog::instance().log(__VA_ARGS__)
#define DEBUG_LOG_IF(cond, ...)                          \
    do {                                                  \
        if (cond) {                                       \
            compressor::debug::DebugLog::instance().log(  \
                __VA_ARGS__);                             \
        }                                                 \
    } while (0)
#else
#define DEBUG_LOG(...) ((void)0)
#define DEBUG_LOG_IF(cond, ...) ((void)0)
#endif