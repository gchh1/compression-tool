#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#endif

namespace compressor::algorithm {

namespace detail {

inline auto workspace_tmp_from_env() -> std::filesystem::path {
#ifdef _WIN32
    wchar_t buf[32768];
    constexpr DWORD kBufChars = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
    const DWORD n = GetEnvironmentVariableW(L"WEBCOMPRESS_WORKSPACE", buf, kBufChars);
    if (n == 0 || n >= kBufChars) {
        return {};
    }
    const std::filesystem::path root(buf);
    const auto tmp = root / L"tmp";
    std::error_code ec;
    std::filesystem::create_directories(tmp, ec);
    return tmp;
#else
    const char* r = std::getenv("WEBCOMPRESS_WORKSPACE");
    if (!r || !r[0]) {
        return {};
    }
    const std::filesystem::path root(r);
    const auto tmp = root / "tmp";
    std::error_code ec;
    std::filesystem::create_directories(tmp, ec);
    return tmp;
#endif
}

inline auto spill_directory() -> std::filesystem::path {
    std::filesystem::path dir = workspace_tmp_from_env();
    if (dir.empty()) {
        dir = std::filesystem::temp_directory_path();
    }
    return dir;
}

inline auto next_temp_suffix() -> uint64_t {
    static std::atomic<uint64_t> seq{0};
    return seq.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace detail

/// Temp file for Out-of-Core DP spill. On Windows we avoid ``FILE_FLAG_DELETE_ON_CLOSE`` and
/// ``FILE_ATTRIBUTE_TEMPORARY`` (both can make Explorer report **0 bytes** while the handle is
/// open). We delete explicitly in the destructor after ``CloseHandle``.
///
/// If ``WEBCOMPRESS_WORKSPACE`` is set (GUI: ``ensure_workspace_layout`` before native compress),
/// files are created under ``<workspace>/tmp/``. Otherwise the OS temp directory is used.
struct TempFile {
    std::string path_;

#ifdef _WIN32
    std::wstring wpath_;
    HANDLE hFile_{INVALID_HANDLE_VALUE};
#else
    std::FILE* fp_{nullptr};
#endif

    TempFile() {
        auto p = detail::spill_directory() /
#ifdef _WIN32
                 ("wc-dp-" + std::to_string(GetCurrentProcessId()) + "-" +
                  std::to_string(GetTickCount64()) + "-" +
                  std::to_string(detail::next_temp_suffix()) + ".tmp");
#else
                 ("wc-dp-" + std::to_string(getpid()) + "-" + std::to_string(rand()) + "-" +
                  std::to_string(detail::next_temp_suffix()) + ".tmp");
#endif
        path_ = p.string();
#ifdef _WIN32
        wpath_ = p.wstring();
        hFile_ = CreateFileW(
            wpath_.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
        fp_ = std::fopen(path_.c_str(), "wb+");
#endif
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;

    ~TempFile() {
#ifdef _WIN32
        if (hFile_ != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(hFile_);
            CloseHandle(hFile_);
            hFile_ = INVALID_HANDLE_VALUE;
        }
        if (!wpath_.empty()) {
            DeleteFileW(wpath_.c_str());
        }
#else
        if (fp_) {
            std::fflush(fp_);
            std::fclose(fp_);
            fp_ = nullptr;
        }
        std::error_code ec;
        std::filesystem::remove(path_, ec);
#endif
    }

    bool valid() const {
#ifdef _WIN32
        return hFile_ != INVALID_HANDLE_VALUE;
#else
        return fp_ != nullptr;
#endif
    }

    void write(const void* data, size_t size) {
        if (size == 0) return;
#ifdef _WIN32
        if (hFile_ == INVALID_HANDLE_VALUE) return;
        const auto* p = static_cast<const std::uint8_t*>(data);
        size_t off = 0;
        while (off < size) {
            DWORD chunk =
                static_cast<DWORD>(std::min<size_t>(size - off, static_cast<size_t>(MAXDWORD)));
            DWORD written = 0;
            if (!WriteFile(hFile_, p + off, chunk, &written, nullptr) || written != chunk) {
                break;
            }
            off += written;
        }
#else
        if (fp_) std::fwrite(data, 1, size, fp_);
#endif
    }

    void readAt(std::uint64_t offset, void* data, size_t size) {
        if (size == 0) return;
#ifdef _WIN32
        if (hFile_ == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER li{};
        li.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(hFile_, li, nullptr, FILE_BEGIN)) return;
        auto* p = static_cast<std::uint8_t*>(data);
        size_t off = 0;
        while (off < size) {
            DWORD chunk =
                static_cast<DWORD>(std::min<size_t>(size - off, static_cast<size_t>(MAXDWORD)));
            DWORD read = 0;
            if (!ReadFile(hFile_, p + off, chunk, &read, nullptr) || read == 0) break;
            off += read;
        }
#else
        if (!fp_) return;
        fseeko(fp_, static_cast<off_t>(offset), SEEK_SET);
        std::fread(data, 1, size, fp_);
#endif
    }

    void flush() {
#ifdef _WIN32
        if (hFile_ != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(hFile_);
        }
#else
        if (fp_) std::fflush(fp_);
#endif
    }
};

}  // namespace compressor::algorithm
