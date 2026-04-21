
#include "CompressorAPI.hpp"

#include <vector>

#include "Archiver.hpp"
#include "LZ78Compressor.hpp"

namespace compressor {
namespace api {

CompressorResult CompressorAPI::compress(const std::vector<File>& files,
                                         int image_quality) {
    std::vector<File> cpp_files;

    auto packed_data = core::Archiver::pack(cpp_files);
    core::LZ78Compressor engine;
    return engine.compress(packed_data);
}

}  // namespace api

}  // namespace compressor