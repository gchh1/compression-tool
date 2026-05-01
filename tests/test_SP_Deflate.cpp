#include <cassert>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Deflate.hpp"
#include "DataChunk.hpp"
#include "MemoryPool.hpp"
#include "StreamProcessor.hpp"

using namespace compressor::algorithm;
using namespace compressor::processor;
using namespace compressor::memory;

int main() {
    auto pool = std::make_shared<MemoryPool>(4, 65536);

    StreamProcessor sp(std::make_unique<Deflate>(), pool);

    // Generate test data (highly repetitive — good for compression)
    std::string input = "Hello, World! This is a compression test. ";
    std::vector<uint8_t> data;
    for (int i = 0; i < 300; ++i) {
        data.insert(data.end(), input.begin(), input.end());
    }
    std::cout << "Original: " << data.size() << " bytes" << std::endl;

    // Push all data
    sp.push(data);
    sp.finish();

    // Collect all output
    std::vector<uint8_t> all_out;
    while (true) {
        auto chunk = sp.pull();
        if (chunk.empty()) break;
        auto v = chunk.view();
        all_out.insert(all_out.end(), v.begin(), v.end());
    }

    std::cout << "Compressed: " << all_out.size() << " bytes (ratio: "
              << (100.0 * all_out.size() / data.size()) << "%)" << std::endl;

    if (all_out.empty()) {
        std::cout << "FAIL: no output produced" << std::endl;
        return 1;
    }

    assert(all_out.size() > 0);
    assert(all_out.size() < data.size());  // must compress repetitive data

    std::cout << "PASS" << std::endl;
    return 0;
}
