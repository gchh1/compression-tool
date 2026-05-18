#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "BitProcessor.hpp"

namespace compressor::algorithm::streaming {

struct ByteBuffer {
    std::vector<uint8_t> leftover;
};


// 流式分块拼接容器
template <typename T>
class VirtualBuffer {
    std::vector<T> chunks_;

public:
    VirtualBuffer() = default;

    VirtualBuffer(T&& c1, T&& c2) {
        if (!c1.empty()) chunks_.push_back(std::move(c1));
        if (!c2.empty()) chunks_.push_back(std::move(c2));
    }


    typename T::value_type& operator[](size_t i) {
        return const_cast<typename T::value_type&>(
            static_cast<const VirtualBuffer*>(this)->operator[](i));
    }

    const typename T::value_type& operator[](size_t i) const {
        for (const auto& chunk : chunks_) {
            if (i < chunk.size()) {
                return chunk[i];
            }
            i -= chunk.size();
        }
        throw std::out_of_range("VirtualBuffer index out of range");
    }

    T pop() {
        if (!chunks_.empty()) {
            T front = std::move(chunks_.front());
            chunks_.erase(chunks_.begin());
            return front;
        }
        return T{};
    }

    T pop_end() {
        if (!chunks_.empty()) {
            T back = std::move(chunks_.back());
            chunks_.pop_back();
            return back;
        }
        return T{};
    }

    void append_front(T container) {
        if (!container.empty()) {
            chunks_.insert(chunks_.begin(), std::move(container));
        }
    }

    void append(T container) {
        if (!container.empty()) {
            chunks_.push_back(std::move(container));
        }
    }

    size_t size() const {
        size_t total = 0;
        for (const auto& chunk : chunks_) {
            total += chunk.size();
        }
        return total;
    }


    bool empty() const {
        return chunks_.empty();
    }

    size_t num_chunks() const {
        return chunks_.size();
    }

    size_t new_chunk_size() const {
        return chunks_.empty() ? 0 : chunks_.back().size();
    }
    size_t prev_chunk_size() const {
        return chunks_.empty() ? 0 : chunks_.front().size();
    }
};

class File_Chunk_Reader {
    std::ifstream file_;
    size_t chunk_size_;
    bool end_{false};
    ByteBuffer buf_;

public:
    File_Chunk_Reader(const std::string& file_path, size_t chunk_size)
        : chunk_size_(chunk_size) {
        file_.open(file_path, std::ios::binary);
        if (!file_.is_open()) end_ = true;
    }

    std::vector<uint8_t> read_chunk() {
        if (end_) return {};
        std::vector<uint8_t> chunk(chunk_size_);
        file_.read(reinterpret_cast<char*>(chunk.data()), chunk_size_);
        size_t read = static_cast<size_t>(file_.gcount());
        if (read < chunk_size_) {
            end_ = true;
            chunk.resize(read);
        }
        if (!buf_.leftover.empty()) {
            chunk.insert(chunk.begin(), buf_.leftover.begin(), buf_.leftover.end());
            buf_.leftover.clear();
        }
        return chunk;
    }

    bool is_end() const { return end_; }

    void setBuf(ByteBuffer buf) { buf_ = buf; }
    ByteBuffer getBuf() const { return buf_; }
};

class File_Chunk_Writer {
    std::ofstream file_;
    ByteBuffer buf_;

public:
    explicit File_Chunk_Writer(const std::string& file_path) {
        file_.open(file_path, std::ios::binary);
    }

    void write_chunk(const std::vector<uint8_t>& data) {
        file_.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
    }

    void setBuf(ByteBuffer buf) { buf_ = buf; }
    ByteBuffer getBuf() const { return buf_; }
};

class Reverse_File_Chunk_Reader {
    std::ifstream file_;
    size_t chunk_size_;
    bool end_{false};
    size_t next_chunk_start_;
    size_t file_size_;
    ByteBuffer buf_;

public:
    Reverse_File_Chunk_Reader(const std::string& file_path, size_t chunk_size)
        : chunk_size_(chunk_size) {
        file_.open(file_path, std::ios::binary);
        if (!file_.is_open()) { end_ = true; return; }
        file_.seekg(0, std::ios::end);
        file_size_ = static_cast<size_t>(file_.tellg());

        size_t remainder = file_size_ % chunk_size_;
        next_chunk_start_ = (remainder == 0)
            ? file_size_ - chunk_size_
            : file_size_ - remainder;
    }

    std::vector<uint8_t> read_chunk() {
        if (end_ || next_chunk_start_ > file_size_) return {};

        file_.seekg(static_cast<std::streamoff>(next_chunk_start_));
        size_t to_read = std::min(chunk_size_, file_size_ - next_chunk_start_);
        std::vector<uint8_t> chunk(to_read);
        file_.read(reinterpret_cast<char*>(chunk.data()),
                   static_cast<std::streamsize>(to_read));

        if (!buf_.leftover.empty()) {
            chunk.insert(chunk.begin(), buf_.leftover.begin(), buf_.leftover.end());
            buf_.leftover.clear();
        }

        if (next_chunk_start_ < chunk_size_) {
            end_ = true;
        } else {
            next_chunk_start_ -= chunk_size_;
        }

        return chunk;
    }

    bool is_end() const { return end_; }

    void setBuf(ByteBuffer buf) { buf_ = buf; }
    ByteBuffer getBuf() const { return buf_; }
};

class Reverse_File_Chunk_Writer {
    std::fstream file_;
    ByteBuffer buf_;
    size_t file_size_{0};
    size_t write_cursor_{0};

public:
    explicit Reverse_File_Chunk_Writer(const std::string& file_path) {
        file_.open(file_path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    }

    void preallocate(size_t nbytes) {
        file_size_ = nbytes;
        write_cursor_ = nbytes;
        if (nbytes == 0) {
            return;
        }
        file_.seekp(static_cast<std::streamoff>(nbytes - 1));
        file_.put(static_cast<char>(0));
        file_.flush();
    }

    /// 从文件尾向前写入一块（预分配后按反序落盘）
    void write_chunk_reverse(const std::vector<uint8_t>& data) {
        if (data.empty()) {
            return;
        }
        if (write_cursor_ < data.size()) {
            throw std::runtime_error("Reverse_File_Chunk_Writer: write past start");
        }
        write_cursor_ -= data.size();
        file_.seekp(static_cast<std::streamoff>(write_cursor_));
        file_.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
    }

    void write_chunk(const std::vector<uint8_t>& data) { write_chunk_reverse(data); }

    size_t remaining_capacity() const { return write_cursor_; }

    void setBuf(ByteBuffer buf) { buf_ = buf; }
    ByteBuffer getBuf() const { return buf_; }
};

}  // namespace compressor::algorithm::streaming
