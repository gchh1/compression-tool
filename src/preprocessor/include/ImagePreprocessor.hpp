/**
 * @file ImagePreprocessor.hpp
 * @author yhc
 * @brief Inheriting from IPreprocessor, using delta algorithm for image
 * preprocession
 * @version 0.1
 * @date 2026-04-19
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "IPreprocessor.hpp"

namespace compressor {
namespace core {

/**
 * @brief
 *
 */
class ImagePreprocessor : public IPreprocessor {
   public:
    auto encode(std::vector<uint8_t> raw_data, int quality)
        -> std::vector<uint8_t>;

    auto decode(std::vector<uint8_t> preprocess_data) -> std::vector<uint8_t>;
};

}  // namespace core

}  // namespace compressor