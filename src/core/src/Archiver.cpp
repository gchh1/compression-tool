#include "Archiver.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor {
namespace core {
/**
 * @brief
 *
 * @param files
 * @return std::vector<uint8_t>
 */
std::vector<uint8_t> Archiver::pack(const std::vector<WebFile> &files) {
    std::vector<uint8_t> result;

    // [Magick Number]: 50 4B 03 04
    result.push_back((MagicNumber >> 24) & 0xFF);
    result.push_back((MagicNumber >> 16) & 0xFF);
    result.push_back((MagicNumber >> 8) & 0xFF);
    result.push_back(MagicNumber & 0xFF);

    // [File Number](4 bytes)
    uint32_t file_size = files.size();
    result.push_back((file_size >> 24) & 0xFF);
    result.push_back((file_size >> 16) & 0xFF);
    result.push_back((file_size >> 8) & 0xFF);
    result.push_back(file_size & 0xFF);

    // Iterate through files
    for (const WebFile &item : files) {
        // [Name Length](2 bytes)
        uint8_t name_size = item.name.size();
        result.push_back((name_size >> 8) & 0xFF);
        result.push_back(name_size & 0xFF);

        // [File Name](${name_size} bytes)
        for (const auto &elem : item.name) {
            result.push_back(elem);
        }

        // [File Size](4 bytes)
        uint32_t file_size_ = item.content.size();
        result.push_back((file_size_ >> 24) & 0xFF);
        result.push_back((file_size_ >> 16) & 0xFF);
        result.push_back((file_size_ >> 8) & 0xFF);
        result.push_back(file_size_ & 0xFF);

        // [File Data]
        result.insert(result.end(), item.content.begin(), item.content.end());
    }

    return result;
}

/**
 * @brief
 *
 * @param data
 * @return std::vector<WebFile>
 */
std::vector<WebFile> Archiver::unpack(const std::vector<uint8_t> &data) {
    std::vector<WebFile> files;
    size_t idx = 0;

    if (data.size() < 8) {
        return files;
    }
    // Check Magic Number
    if (data[idx++] != ((MagicNumber >> 24) & 0xFF) ||
        data[idx++] != ((MagicNumber >> 16) & 0xFF) ||
        data[idx++] != ((MagicNumber >> 8) & 0xFF) ||
        data[idx++] != (MagicNumber & 0xFF)) {
        return files;
    }

    // Read file number
    uint32_t file_number = (data[idx] << 24) | (data[idx + 1] << 16) |
                           (data[idx + 2] << 8) | data[idx + 3];
    idx += 4;

    // Unpack file one by one
    while (file_number--) {
        // Read file name length
        uint16_t name_length = (data[idx] << 8) | data[idx + 1];
        idx += 2;

        // Read file name
        std::string name;
        while (name_length--) {
            name += data[idx++];
        }

        // Read file content
        // Read file size
        uint32_t file_size = (data[idx] << 24) | (data[idx + 1] << 16) |
                             (data[idx + 2] << 8) | data[idx + 3];
        idx += 4;

        std::vector<uint8_t> content(data.begin() + idx,
                                     data.begin() + idx + file_size);
        idx += file_size;

        files.push_back({name, content});
    }

    return files;
}

}  // namespace core

}  // namespace compressor