#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor::algorithm {

enum class VideoFormat { H264 };

struct RawVideoInfo {
    int width = 0;
    int height = 0;
    int fps = 30;
    int num_frames = 0;
    /// 0 = RGB24, 1 = YUV420
    int pixel_format = 0;
};

/// Parse raw video header (16 bytes: width, height, fps, num_frames, format x4)
auto parse_raw_video_header(const std::vector<uint8_t>& data) -> RawVideoInfo;

/// Build raw video header + frame data
auto build_raw_video(const std::vector<uint8_t>& frame_data,
                     int width, int height, int fps,
                     int num_frames, int pixel_format) -> std::vector<uint8_t>;

auto video_compress(const std::vector<uint8_t>& raw_frames,
                    VideoFormat format = VideoFormat::H264,
                    int quality = 26) -> std::vector<uint8_t>;

auto video_decompress(const std::vector<uint8_t>& h264_data) -> std::vector<uint8_t>;

namespace h264 {

/// RGB24 → YUV420 conversion
void rgb24_to_yuv420(const uint8_t* rgb, int width, int height,
                     std::vector<uint8_t>& y, std::vector<uint8_t>& u, std::vector<uint8_t>& v);

/// Encode a single frame as H.264 IDR slice (Annex B)
auto encode_idr_slice(const uint8_t* y_plane, const uint8_t* u_plane,
                      const uint8_t* v_plane,
                      int width, int height, int qp,
                      int frame_num, int idr_pic_id) -> std::vector<uint8_t>;

/// Generate SPS NAL unit
auto generate_sps(int width, int height, int qp) -> std::vector<uint8_t>;

/// Generate PPS NAL unit
auto generate_pps(int qp) -> std::vector<uint8_t>;

}  // namespace h264

}  // namespace compressor::algorithm