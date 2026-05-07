#include "AlgorithmFactory.hpp"

#include <memory>

#include "Deflate.hpp"
#include "Delta.hpp"
#include "Inflate.hpp"
#include "LZMine.hpp"
#include "LZSS.hpp"
#include "MyFlate.hpp"
#include "StreamingAdapter.hpp"

namespace compressor::core {

auto createAlgorithm(AlgorithmID id) -> std::unique_ptr<algorithm::IAlgorithm> {
    using algorithm::StreamingCompressAdapter;
    using algorithm::StreamingDecompressAdapter;

    switch (id) {
        case AlgorithmID::None:
            return nullptr;
        case AlgorithmID::Deflate:
            return std::make_unique<StreamingCompressAdapter>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    algorithm::Deflate deflater;
                    std::vector<uint8_t> out(data.size() + 1024);
                    auto status = deflater.process(data, out, true);
                    out.resize(status.bytes_produced);
                    return out;
                });
        case AlgorithmID::Inflate:
            return std::make_unique<StreamingDecompressAdapter>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    algorithm::Inflate inflate;
                    std::vector<uint8_t> out(std::max(data.size() * 4 + 65536, size_t(2097152)));
                    auto status = inflate.process(data, out, true);
                    out.resize(status.bytes_produced);
                    return out;
                });
        case AlgorithmID::DeltaEncode:
            return std::make_unique<algorithm::DeltaEncode>();
        case AlgorithmID::DeltaDecode:
            return std::make_unique<algorithm::DeltaDecode>();
        case AlgorithmID::MyFlate:
            return std::make_unique<algorithm::MyFlate>();
        case AlgorithmID::LZSS:
            return std::make_unique<StreamingCompressAdapter>(
                [](const std::vector<uint8_t>& data) {
                    return algorithm::LZSS::compress(data);
                });
        case AlgorithmID::LZSSDecompress:
            return std::make_unique<StreamingDecompressAdapter>(
                [](const std::vector<uint8_t>& data) {
                    return algorithm::LZSS::decompress(data);
                });
        case AlgorithmID::LZMine:
            return std::make_unique<StreamingCompressAdapter>(
                [](const std::vector<uint8_t>& data) {
                    algorithm::LZMine lzmine;
                    size_t search_size = std::min(data.size() / 2, size_t(32768));
                    size_t lookahead_size = std::min(data.size() / 4, size_t(258));
                    if (search_size < 16) search_size = 16;
                    if (lookahead_size < 4) lookahead_size = 4;
                    lzmine.autoByteLength(search_size, lookahead_size);
                    return lzmine.compress(data, search_size, lookahead_size);
                });
        case AlgorithmID::LZMineDecompress:
            return std::make_unique<StreamingDecompressAdapter>(
                [](const std::vector<uint8_t>& data) {
                    algorithm::LZMine lzmine;
                    return lzmine.decompress(data);
                });
    }
    return nullptr;
}

}
