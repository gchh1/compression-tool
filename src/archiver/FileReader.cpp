#include "FileReader.hpp"

#ifndef __EMSCRIPTEN__

#include <algorithm>
#include <cstring>

namespace compressor::archiver {

FileReader::FileReader(const std::string& path)
    : file_(path, std::ios::binary | std::ios::in) {
    if (!file_) return;
    file_.seekg(0, std::ios::end);
    size_ = static_cast<uint64_t>(file_.tellg());
}

auto FileReader::read(uint64_t offset, std::span<uint8_t> buffer) -> size_t {
    if (!file_ || offset >= size_) return 0;
    file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file_) return 0;
    size_t to_read =
        std::min(static_cast<size_t>(size_ - offset), buffer.size());
    file_.read(reinterpret_cast<char*>(buffer.data()),
               static_cast<std::streamsize>(to_read));
    return static_cast<size_t>(file_.gcount());
}

auto FileReader::size() const -> uint64_t { return size_; }

}  // namespace compressor::archiver

#endif  // __EMSCRIPTEN__
