#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "IAlgorithm.hpp"

namespace compressor::algorithm {

/// Maps GUI / compressor knobs to Google Brotli encoder settings.
struct BrotliParams {
    size_t window_size{65536};
    size_t min_match{3};
    size_t max_chain_length{256};
};

/// One-shot encode/decode using libbrotli (standard ``.br`` bitstream per call).
auto brotli_encode(const std::vector<uint8_t>& data,
                   const BrotliParams& params = {}) -> std::vector<uint8_t>;
auto brotli_decode(const std::vector<uint8_t>& data) -> std::vector<uint8_t>;

/// Pipeline node: buffers input until ``is_last_chunk``, then emits one Brotli stream.
class BrotliCompress : public AlgorithmBase {
   public:
    BrotliCompress(size_t window_size = 65536, size_t min_match = 3,
                   size_t max_chain_length = 256);

    auto reset() -> void override;

   protected:
    auto handle(AlgorithmStatus& status, bool is_last_chunk) -> void override;

   private:
    BrotliParams params_;
    std::vector<uint8_t> input_buffer_;
    bool finished_{false};
};

class BrotliDecompress : public AlgorithmBase {
   public:
    BrotliDecompress();

    auto reset() -> void override;

   protected:
    auto handle(AlgorithmStatus& status, bool is_last_chunk) -> void override;

   private:
    std::vector<uint8_t> input_buffer_;
    bool finished_{false};
};

}  // namespace compressor::algorithm
