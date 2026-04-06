#ifndef CORE_HPP
#define CORE_HPP

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace core{
    using u8 = uint8_t;
    using u16 = uint16_t;
    using u32 = uint32_t;
    using u64 = uint64_t;


    enum class endian:u8{little=0,big=1};

    constexpr endian ENDIAN = endian::little; //使用 constexpr 而不是 const 编译的时候确定值而不是运行时候

    template<typename T>
    inline std::vector<u8> to_u8(const T& data){
        constexpr size_t size = sizeof(T);
        std::vector<u8> out(size);
        if constexpr (ENDIAN == endian::little){
            for (size_t i=0;i<size;i++){
                out[i]=static_cast<u8>((data >> (i * 8)) & 0xFF);
            }
        }else{
            for (size_t i=0;i<size;i++){
                out[i]=static_cast<u8>((data >> ((size - 1 - i) * 8)) & 0xFF);
            }
        }
        return out;
    }
    inline std::vector<u8> to_u8(const size_t& data,size_t bytelength){
        std::vector<u8> out(bytelength);
        if constexpr (ENDIAN == endian::little){
            for (size_t i=0;i<bytelength;i++){
                out[i]=static_cast<u8>((data >> (i * 8)) & 0xFF);
            }
        }else{
            for (size_t i=0;i<bytelength;i++){
                out[i]=static_cast<u8>((data >> ((bytelength - 1 - i) * 8)) & 0xFF);
            }
        }
        return out;
    }
    
    // 从字节数组还原为 POD 类型
    template<typename T>
    inline T from_u8(const uint8_t* data, size_t bytelength) {

        
        T out = 0;
        if constexpr (ENDIAN == endian::little){
            for (size_t i=0;i<bytelength;i++){
                out |= static_cast<T>(data[i]) << (i * 8);
            }
        }else{
            for (size_t i=0;i<bytelength;i++){
                out |= static_cast<T>(data[i]) << ((bytelength - 1 - i) * 8);
            }
        }
        return out;
    }
    
    // 迭代器版本的重载
    template<typename T, typename Iterator>
    inline T from_u8(Iterator begin, size_t bytelength) {
        return from_u8<T>(&(*begin), bytelength);
    }
    
    inline void concat(std::vector<u8>& dst, const std::vector<u8>& src){
        dst.insert(dst.end(), 
                src.begin(), 
                src.end());
    }
    inline void concat(std::vector<u8>& dst, std::vector<u8>::const_iterator start, std::vector<u8>::const_iterator end){
        dst.insert(dst.end(),start,end);
    }
    namespace algorithm{
    }

}





#endif

