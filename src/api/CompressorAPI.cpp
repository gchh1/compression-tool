
#include "CompressorAPI.hpp"

#include <vector>

#include "Archiver.hpp"
#include "DeflateCompressor.hpp"

namespace compressor {
namespace api {

CompressorResult CompressorAPI::compress(const std::vector<File>& files,
                                         int image_quality) {
    std::vector<File> cpp_files;

    auto packed_data = core::Archiver::pack(cpp_files);
    core::DeflateCompressor engine;
    return engine.compress(packed_data);
}

}  // namespace api

}  // namespace compressor