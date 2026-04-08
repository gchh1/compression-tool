#include <cstdint>
#include <vector>

#include "compressor.hpp"

namespace compressor {
namespace core {
class LZSSCompressor : public ICompressor {
   public:
    CompressResult compress(const std::vector<uint8_t>& data) override;
    CompressResult decompress(const std::vector<uint8_t>& data) override;
    std::string get_algorithm_name(void) override { return "LZSS"; }
};

}  // namespace core

}  // namespace compressor