#pragma once

// Inlcude lib here
#include "Deflate.hpp"
#include "compressor.hpp"

namespace compressor {
namespace core {

class DeflateCompressor : public ICompressor {
   public:
    CompressorResult compress(
        const std::vector<uint8_t>& original_data) override;
    CompressorResult decompress(
        const std::vector<uint8_t>& compressed_data) override;

    inline std::string get_algorithm_name(void) override {
        return "Deflate (LZ77Fast + Huffman)";
    }

   private:
    /** @brief Serialize Deflate Token to byte stream that Huffman needed */
    std::vector<uint8_t> serializeTokens(
        const std::vector<algorithm::Token>& tokens);

    /** @brief Deserialize byte stream to Deflate Token, for decompress */
    std::vector<algorithm::Token> deserializeTokens(
        const std::vector<uint8_t>& data);
};

}  // namespace core
}  // namespace compressor