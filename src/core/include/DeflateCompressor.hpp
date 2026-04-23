#pragma once

// Inlcude lib here
#include <cstdint>
#include <vector>

#include "Deflate.hpp"
#include "ICompressor.hpp"


namespace compressor {
namespace core {

class DeflateCompressor : public ICompressor {
   public:
    auto compress(std::vector<uint8_t> original_data)
        -> CompressorResult override;

    auto decompress(std::vector<uint8_t> compressed_data)
        -> CompressorResult override;

    inline std::string get_algorithm_name(void) override {
        return "Deflate (LZ77Fast + Huffman)";
    }

   private:
    /** @brief Serialize Deflate Token to byte stream that Huffman needed */
    auto serializeTokens(std::vector<algorithm::Token> tokens)
        -> std::vector<uint8_t>;

    /** @brief Deserialize byte stream to Deflate Token, for decompress */
    auto deserializeTokens(std::vector<uint8_t> data)
        -> std::vector<algorithm::Token>;
};

}  // namespace core
}  // namespace compressor