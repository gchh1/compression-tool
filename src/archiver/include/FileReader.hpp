#pragma once

#ifndef __EMSCRIPTEN__

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>

#include "IDataReader.hpp"

namespace compressor::archiver {

class FileReader : public IDataReader {
   public:
    explicit FileReader(const std::string& path);

    auto read(uint64_t offset, std::span<uint8_t> buffer) -> size_t override;
    auto size() const -> uint64_t override;

   private:
    std::ifstream file_;
    uint64_t size_{0};
};

}  // namespace compressor::archiver

#endif  // __EMSCRIPTEN__
