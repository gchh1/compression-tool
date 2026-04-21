/**
 * @file IPreprocessor.hpp
 * @author yhc
 * @brief For raw file byte stream, we need to preprocess them through specific
 *        algorithm in order to increase the compress ratio. For example, for
 *        image like .png and .jpg, we use delta algorithm. This is the
 *        interface of preprocessor.
 * @version 0.1
 * @date 2026-04-19
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cstdint>
#include <vector>
namespace compressor {
namespace core {
class IPreprocessor {
   public:
    virtual ~IPreprocessor() = default;

    virtual auto encode(std::vector<uint8_t> raw_data, int quality)
        -> std::vector<uint8_t> = 0;

    virtual auto decode(std::vector<uint8_t> preprocess_data)
        -> std::vector<uint8_t> = 0;
};

}  // namespace core

}  // namespace compressor