#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "api.hpp"

using namespace compressor;

int main() {
    std::cout << "=== PackWriter/PackReader Round-Trip Test ===" << std::endl;

    // Build multi-file input
    std::vector<api::WebFile> input;

    {
        api::WebFile f;
        f.name = "index.html";
        std::string html = "<html><body><h1>Hello World</h1></body></html>";
        f.content.assign(html.begin(), html.end());
        input.push_back(f);
    }
    {
        api::WebFile f;
        f.name = "style.css";
        std::string css = "body{color:red;margin:0;padding:0}h1{font-size:2em}";
        f.content.assign(css.begin(), css.end());
        input.push_back(f);
    }
    {
        api::WebFile f;
        f.name = "app.js";
        std::string js = "function hello(){return'Hello World';}"
                         "console.log(hello());";
        f.content.assign(js.begin(), js.end());
        input.push_back(f);
    }

    std::cout << "Input: " << input.size() << " files" << std::endl;
    for (const auto& f : input) {
        std::cout << "  " << f.name << ": " << f.content.size() << "B"
                  << std::endl;
    }

    // Pack + compress
    auto packed = api::packAndCompress(input, api::AlgorithmID::Deflate);
    size_t total_orig = 0;
    for (const auto& f : input) total_orig += f.content.size();
    std::cout << "Packed: " << total_orig << "B -> " << packed.size() << "B ("
              << (100.0 * packed.size() / total_orig) << "%)" << std::endl;

    assert(!packed.empty());

    // Unpack + decompress
    auto unpacked = api::decompressAndUnpack(packed);
    assert(unpacked.size() == input.size());
    std::cout << "Unpacked: " << unpacked.size() << " files" << std::endl;

    // Verify each file
    for (size_t i = 0; i < unpacked.size(); ++i) {
        std::cout << "  " << unpacked[i].name << ": "
                  << unpacked[i].content.size() << "B" << std::endl;
        assert(unpacked[i].name == input[i].name);
        assert(unpacked[i].content == input[i].content);
    }

    std::cout << "\n[PASS] PackWriter round-trip verified" << std::endl;
    return 0;
}
