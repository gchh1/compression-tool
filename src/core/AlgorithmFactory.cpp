#include "AlgorithmFactory.hpp"

#include <memory>

#include "Brotli.hpp"
#include "Deflate.hpp"
#include "Delta.hpp"
#include "Inflate.hpp"
#include "LZDP.hpp"
#include "LZSS.hpp"
#include "DPFlate.hpp"
#include "StreamingAdapter.hpp"
#include "Zstd.hpp"

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
        case AlgorithmID::DPFlate:
            return std::make_unique<algorithm::DPFlate>();
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
        case AlgorithmID::LZDP:
            return std::make_unique<StreamingCompressAdapter>(
                [](const std::vector<uint8_t>& data) {
                    algorithm::LZDP lzdp;
                    size_t search_size = std::min(data.size() / 2, size_t(32768));
                    size_t lookahead_size = std::min(data.size() / 4, size_t(258));
                    if (search_size < 16) search_size = 16;
                    if (lookahead_size < 4) lookahead_size = 4;
                    lzdp.autoBitWidth(search_size, lookahead_size);
                    return lzdp.compress(data, search_size, lookahead_size);
                });
        case AlgorithmID::LZDPDecompress:
            return std::make_unique<StreamingDecompressAdapter>(
                [](const std::vector<uint8_t>& data) {
                    algorithm::LZDP lzdp;
                    return lzdp.decompress(data);
                });
        case AlgorithmID::Brotli:
            return std::make_unique<StreamingCompressAdapter>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    algorithm::BrotliCompress brotli(65536, 3, 256);
                    std::vector<uint8_t> out(data.size() + 1024);
                    auto status = brotli.process(data, out, true);
                    out.resize(status.bytes_produced);
                    return out;
                });
        case AlgorithmID::BrotliDecompress:
            return std::make_unique<StreamingDecompressAdapter>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    algorithm::BrotliDecompress decompress;
                    std::vector<uint8_t> out(std::max(data.size() * 4 + 65536, size_t(2097152)));
                    auto status = decompress.process(data, out, true);
                    out.resize(status.bytes_produced);
                    return out;
                });
        case AlgorithmID::Zstd:
            return std::make_unique<StreamingCompressAdapter>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    return algorithm::ZstdCompress::compress(data, 3);
                });
        case AlgorithmID::ZstdDecompress:
            return std::make_unique<StreamingDecompressAdapter>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    algorithm::ZstdDecompress decomp;
                    return decomp.decompress(data);
                });
    }
    return nullptr;
}

}
