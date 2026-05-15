#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "EntryHeader.hpp"

namespace compressor::archiver {

struct File {
    std::string filepath;
    std::vector<uint8_t> context;
};

class Archiver {
   public:
    static auto pack(const std::vector<File>& files) -> std::vector<uint8_t> {
        std::vector<uint8_t> output;

        uint32_t file_count = static_cast<uint32_t>(files.size());
        for (int i = 0; i < 4; ++i)
            output.push_back(static_cast<uint8_t>((file_count >> (i * 8)) & 0xFF));

        for (const auto& f : files) {
            archiver::EntryHeader hdr;
            hdr.filepath = f.filepath;
            hdr.algo_chain = {core::AlgorithmID::None};
            hdr.original_size = f.context.size();
            hdr.compressed_size = f.context.size();

            auto hdr_bytes = hdr.serialize();
            output.insert(output.end(), hdr_bytes.begin(), hdr_bytes.end());
            output.insert(output.end(), f.context.begin(), f.context.end());
        }

        return output;
    }

    static auto unpack(const std::vector<uint8_t>& data) -> std::vector<File> {
        std::vector<File> files;
        if (data.size() < 4) return files;

        uint32_t file_count = 0;
        for (int i = 0; i < 4; ++i)
            file_count |= static_cast<uint32_t>(data[i]) << (i * 8);

        size_t pos = 4;
        for (uint32_t i = 0; i < file_count && pos < data.size(); ++i) {
            auto remaining = std::span<const uint8_t>(data.data() + pos, data.size() - pos);
            auto result = archiver::EntryHeader::deserialize(remaining);
            if (!result) break;

            auto& [hdr, hdr_size] = *result;
            pos += hdr_size;

            if (pos + hdr.compressed_size > data.size()) break;

            File f;
            f.filepath = hdr.filepath;
            f.context.assign(data.data() + pos, data.data() + pos + hdr.compressed_size);
            pos += hdr.compressed_size;
            files.push_back(std::move(f));
        }

        return files;
    }
};

}  // namespace compressor::archiver
