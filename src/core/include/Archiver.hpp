/**
 * @file Archiver.hpp
 * @author yhc
 * @brief Header for Archiver, aimming to pack website files into one byte
 *        stream
 * @version 0.1
 * @date 2026-04-12
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cstdint>
#include <string>
#include <vector>

namespace compressor {
namespace core {
/** @brief File structure */
struct WebFile {
    std::string name;
    std::vector<uint8_t> content;
};

const uint32_t MagicNumber = 0x503B0304;

/** @brief Provideing static pack/unpack API.
 *         Package protocol:
 *               === overall
 *               [Magick Number](4 bytes): 50 4B 03 04
 *               [File Number](4 bytes)
 *               === file
 *               [Name Length](2 bytes)
 *               [File Name](n bytes)
 *               [File Size](4 bytes)
 *               [File Data](n bytes)
 */
class Archiver {
   public:
    static std::vector<uint8_t> pack(const std::vector<WebFile>& files);
    static std::vector<WebFile> unpack(const std::vector<uint8_t>& data);
};

}  // namespace core

}  // namespace compressor