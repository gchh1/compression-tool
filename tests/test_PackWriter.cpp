#include <array>
#include <cassert>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "DataChunk.hpp"
#include "MemoryPool.hpp"
#include "PackWriter.hpp"
#include "Pipeline.hpp"

using namespace compressor::archiver;
using namespace compressor::memory;

int main() {
    std::cout << "=== Test: Deflate compression via PackWriter ===" << std::endl;
    {
        auto pool = std::make_shared<MemoryPool>(8, 65536);
        PackWriter writer(pool);

        writer.beginFile("test.txt", std::array{AlgorithmID::Deflate});

        // Generate repetitive data (good for compression)
        std::string input = "Hello, World! This is a compression test. ";
        std::vector<uint8_t> data;
        for (int i = 0; i < 300; ++i) {
            data.insert(data.end(), input.begin(), input.end());
        }
        std::cout << "Original: " << data.size() << " bytes" << std::endl;

        writer.pushFileData(data);
        writer.endFile();
        writer.finish();

        // Collect all output
        std::vector<uint8_t> compressed;
        while (true) {
            auto out = writer.pullOutput();
            if (out.empty()) break;
            compressed.insert(compressed.end(), out.begin(), out.end());
            writer.consumeOutput(out.size());
        }

        std::cout << "Total compressed: " << compressed.size() << " bytes (ratio: "
                  << (100.0 * compressed.size() / data.size()) << "%)" << std::endl;

        assert(!compressed.empty());
        assert(compressed.size() < data.size());
        std::cout << "PASS" << std::endl;
    }

    return 0;
}
