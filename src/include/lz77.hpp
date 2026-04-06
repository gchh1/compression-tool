#ifndef LZ77_HPP
#define LZ77_HPP

// #include <cstddef>
#include <cstdint>
#include <vector>

#include "core.hpp"

//保留 dll 输出设计
#ifdef _WIN32
#define LZ77_API __declspec(dllexport)
#else
#define LZ77_API
#endif


namespace core{
    namespace algorithm{
        /**
         * @brief optimized LZ77 algorithm
         *
         */

        enum class lz77MatchType{//要确保这个类型接口外部可以使用
            KMPNEXT,  // KMP 匹配算法
        };

        LZ77_API class LZ77 {

        public:
            struct lz77Triple {
                size_t offset;
                size_t length;
                u8 next_byte;
                lz77Triple(size_t a,size_t b,u8 c):offset(a),length(b),next_byte(c){}
                lz77Triple():offset(0),length(0),next_byte('\0'){}
            };
        private:
            size_t SEARCH_BYTELENGTH;            
            size_t LOOKAHEAD_BYTELENGTH;

            size_t max_search_size;//在literalrun中使用的最大搜索窗口大小
            size_t max_look_size;//在literalrun中使用的最大预查窗口大小

            friend struct maxheap;

            std::vector<size_t> kmp_next(std::vector<u8>::const_iterator pattern, size_t len);
   
            // lz77Triple kmpMatch(
            //     vector<u8>::iterator search_window,size_t search_window_size,
            //     vector<u8>::iterator lookahead_window,size_t lookahead_window_size,
            // );

            lz77Triple random(std::vector<lz77Triple> arrs);//用于nongreedy策略的辅助函数
            
            std::vector<lz77Triple> kmpMatch(
                std::vector<u8>::const_iterator search_window,size_t search_window_size,
                std::vector<u8>::const_iterator lookahead_window,size_t lookahead_window_size,
                size_t range=1
            );

            std::vector<u8> literalrun(
                const std::vector<u8>& input
            );

        public:
            LZ77(size_t search_bytelength=2, size_t look_bytelength=2):SEARCH_BYTELENGTH(search_bytelength),LOOKAHEAD_BYTELENGTH(look_bytelength){
                // 最小单位就是8位，这里的设计是考虑huffman按byte编码的情况，另外最小单位为1位更复杂

                size_t tmp=SEARCH_BYTELENGTH;
                max_search_size = 0xFF;
                max_look_size = 0xFF;
                while(tmp>1){
                    max_search_size <<= 8;
                    max_search_size |= 0xFF;
                    tmp--;
                }
                tmp = LOOKAHEAD_BYTELENGTH;
                while(tmp>1){
                    max_look_size <<= 8;
                    max_look_size |= 0xFF;
                    tmp--;
                }


            }
            std::vector<u8> Compress(
                const std::vector<u8>& input,
                size_t search_size,    
                size_t lookahead_size,
                lz77MatchType match_type = lz77MatchType::KMPNEXT
            );
            std::vector<u8> Compress_ultra(
                const std::vector<u8>& input,
                size_t search_size,    
                size_t lookahead_size,
                size_t range = 3,
                lz77MatchType match_type = lz77MatchType::KMPNEXT
            );
            std::vector<u8> Decompress(const std::vector<u8>& input);
            

        };

    }
}

#endif
