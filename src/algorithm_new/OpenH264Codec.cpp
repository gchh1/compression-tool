#include "OpenH264Codec.hpp"
#include "VideoCodec.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

// #define OPENH264_DIAG 1

#include "codec_api.h"
#include "codec_app_def.h"
#include "codec_def.h"

namespace compressor::algorithm {

namespace {

static void openh264_trace(void* ctx, int level, const char* msg) {
    (void)ctx;
    (void)level;
    std::fprintf(stderr, "[OpenH264] %s\n", msg);
}

void rgb24_to_yuv420(const uint8_t* rgb, int w, int h,
                     std::vector<uint8_t>& y,
                     std::vector<uint8_t>& u,
                     std::vector<uint8_t>& v) {
    y.resize(static_cast<size_t>(w * h));
    u.resize(static_cast<size_t>((w / 2) * (h / 2)));
    v.resize(static_cast<size_t>((w / 2) * (h / 2)));

    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            size_t idx = static_cast<size_t>(j * w + i) * 3;
            int R = rgb[idx];
            int G = rgb[idx + 1];
            int B = rgb[idx + 2];
            y[static_cast<size_t>(j * w + i)] = static_cast<uint8_t>(
                ((66 * R + 129 * G + 25 * B + 128) >> 8) + 16);
            if (j % 2 == 0 && i % 2 == 0) {
                size_t uv_idx = static_cast<size_t>((j / 2) * (w / 2) + (i / 2));
                u[uv_idx] = static_cast<uint8_t>(
                    ((-38 * R - 74 * G + 112 * B + 128) >> 8) + 128);
                v[uv_idx] = static_cast<uint8_t>(
                    ((112 * R - 94 * G - 18 * B + 128) >> 8) + 128);
            }
        }
    }
}

void yuv420_to_rgb24(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                     int w, int h, uint8_t* rgb) {
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            int Y = y[j * w + i];
            int U = u[(j / 2) * (w / 2) + (i / 2)] - 128;
            int V = v[(j / 2) * (w / 2) + (i / 2)] - 128;
            int idx = (j * w + i) * 3;
            int R = ((298 * (Y - 16) + 409 * V + 128) >> 8);
            int G = ((298 * (Y - 16) - 100 * U - 208 * V + 128) >> 8);
            int B = ((298 * (Y - 16) + 516 * U + 128) >> 8);
            rgb[idx]     = static_cast<uint8_t>(std::clamp(R, 0, 255));
            rgb[idx + 1] = static_cast<uint8_t>(std::clamp(G, 0, 255));
            rgb[idx + 2] = static_cast<uint8_t>(std::clamp(B, 0, 255));
        }
    }
}

}  // anonymous namespace

auto openh264_compress(const std::vector<uint8_t>& raw_data,
                       int quality) -> std::vector<uint8_t> {
    auto info = parse_raw_video_header(raw_data);
    if (info.width <= 0 || info.height <= 0 || info.num_frames <= 0)
        return {};
    if (info.width > 4096 || info.height > 4096 || info.num_frames > 10000)
        return {};

    int w = info.width;
    int h = info.height;
    int fps = (info.fps > 0) ? info.fps : 30;
    int num_frames = info.num_frames;
    const uint8_t* frame_ptr = raw_data.data() + 16;
    size_t frame_size = static_cast<size_t>(w) * static_cast<size_t>(h) * 3;
    size_t total_frame_data = frame_size * static_cast<size_t>(num_frames);
    if (raw_data.size() < 16 + total_frame_data)
        return {};

    // ── Create encoder ──
    ISVCEncoder* encoder = nullptr;
    if (WelsCreateSVCEncoder(&encoder) != 0 || !encoder)
        return {};

    // ── Configure ──
    SEncParamExt param;
    encoder->GetDefaultParams(&param);

    // Enable OpenH264 internal logging
    WelsTraceCallback trace_cb = openh264_trace;
    encoder->SetOption(ENCODER_OPTION_TRACE_CALLBACK, &trace_cb);
    int trace_level = WELS_LOG_DEBUG;
    encoder->SetOption(ENCODER_OPTION_TRACE_LEVEL, &trace_level);

    param.iUsageType = CAMERA_VIDEO_REAL_TIME;
    param.fMaxFrameRate = static_cast<float>(fps);
    param.iPicWidth = w;
    param.iPicHeight = h;
    param.iTargetBitrate = std::max(10000, w * h * 3);
    param.iMaxBitrate = param.iTargetBitrate * 2;
    param.iTemporalLayerNum = 1;
    param.iSpatialLayerNum = 1;
    param.bEnableFrameSkip = 1;
    param.eSpsPpsIdStrategy = CONSTANT_ID;
    param.sSpatialLayers[0].uiProfileIdc = PRO_BASELINE;
    param.sSpatialLayers[0].iVideoWidth = w;
    param.sSpatialLayers[0].iVideoHeight = h;
    param.sSpatialLayers[0].iSpatialBitrate = param.iTargetBitrate;
    param.sSpatialLayers[0].iMaxSpatialBitrate = param.iTargetBitrate * 2;
    param.sSpatialLayers[0].iDLayerQp = std::clamp(quality, 0, 51);
    param.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
    param.iMultipleThreadIdc = 1;

    int init_ret = encoder->InitializeExt(&param);
    std::fprintf(stderr, "[DIAG] InitializeExt returned %d\n", init_ret);
    if (init_ret != 0) {
        WelsDestroySVCEncoder(encoder);
        return {};
    }

    int video_format = videoFormatI420;
    int setopt_ret = encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &video_format);
    std::fprintf(stderr, "[DIAG] SetOption(DATAFORMAT) returned %d\n", setopt_ret);

    // ── Encode frames ──
    std::vector<uint8_t> out;
    std::vector<uint8_t> y_plane, u_plane, v_plane;

    SFrameBSInfo bs_info;
    SSourcePicture pic;

    for (int fnum = 0; fnum < num_frames; ++fnum) {
        const uint8_t* rgb_frame = frame_ptr + static_cast<size_t>(fnum) * frame_size;

        rgb24_to_yuv420(rgb_frame, w, h, y_plane, u_plane, v_plane);

        std::memset(&pic, 0, sizeof(pic));
        pic.iPicWidth = w;
        pic.iPicHeight = h;
        pic.iColorFormat = videoFormatI420;
        pic.iStride[0] = w;
        pic.iStride[1] = w / 2;
        pic.iStride[2] = w / 2;
        pic.pData[0] = y_plane.data();
        pic.pData[1] = u_plane.data();
        pic.pData[2] = v_plane.data();

        std::memset(&bs_info, 0, sizeof(bs_info));

        int enc_ret = encoder->EncodeFrame(&pic, &bs_info);
        std::fprintf(stderr, "[DIAG] EncodeFrame frame %d returned %d, eFrameType=%d, iLayerNum=%d\n",
                     fnum, enc_ret, bs_info.eFrameType, bs_info.iLayerNum);
        if (enc_ret != 0)
            continue;

        for (int layer = 0; layer < bs_info.iLayerNum; ++layer) {
            const SLayerBSInfo& layer_info = bs_info.sLayerInfo[layer];
            size_t offset = 0;
            for (int n = 0; n < layer_info.iNalCount; ++n) {
                int nal_len = layer_info.pNalLengthInByte[n];
                out.insert(out.end(),
                           layer_info.pBsBuf + offset,
                           layer_info.pBsBuf + offset + nal_len);
                offset += static_cast<size_t>(nal_len);
            }
        }
    }

    // ── Force encoder to flush remaining frames ──
    std::memset(&pic, 0, sizeof(pic));
    std::memset(&bs_info, 0, sizeof(bs_info));
    encoder->EncodeFrame(nullptr, &bs_info);

    encoder->Uninitialize();
    WelsDestroySVCEncoder(encoder);

    if (out.empty())
        return {};

    return out;
}

auto openh264_decompress(const std::vector<uint8_t>& h264_data) -> std::vector<uint8_t> {
    if (h264_data.size() < 8)
        return {};

    // ── Create decoder ──
    ISVCDecoder* decoder = nullptr;
    if (WelsCreateDecoder(&decoder) != 0 || !decoder)
        return {};

    SDecodingParam dec_param;
    std::memset(&dec_param, 0, sizeof(dec_param));
    dec_param.uiTargetDqLayer = 255;
    dec_param.eEcActiveIdc = ERROR_CON_DISABLE;
    dec_param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;

    if (decoder->Initialize(&dec_param) != 0) {
        WelsDestroyDecoder(decoder);
        return {};
    }

    // ── Decode ──
    uint8_t* y_buf = nullptr;
    SBufferInfo buf_info;

    std::memset(&buf_info, 0, sizeof(buf_info));
    DECODING_STATE state = decoder->DecodeFrameNoDelay(
        h264_data.data(),
        static_cast<int>(h264_data.size()),
        &y_buf, &buf_info);

    if (state != dsErrorFree || !y_buf || !buf_info.iBufferStatus) {
        decoder->Uninitialize();
        WelsDestroyDecoder(decoder);
        return {};
    }

    int w = buf_info.UsrData.sSystemBuffer.iWidth;
    int h = buf_info.UsrData.sSystemBuffer.iHeight;
    if (w <= 0 || h <= 0) {
        decoder->Uninitialize();
        WelsDestroyDecoder(decoder);
        return {};
    }

    int stride_y = buf_info.UsrData.sSystemBuffer.iStride[0];
    int stride_u = buf_info.UsrData.sSystemBuffer.iStride[1];

    // Copy Y plane (accounting for stride)
    std::vector<uint8_t> y_plane(static_cast<size_t>(w * h));
    for (int j = 0; j < h; ++j)
        std::memcpy(&y_plane[static_cast<size_t>(j * w)],
                    &y_buf[static_cast<size_t>(j * stride_y)],
                    static_cast<size_t>(w));

    // OpenH264 packs: Y plane | U plane | V plane (contiguous)
    uint8_t* u_buf = y_buf + static_cast<size_t>(stride_y) * static_cast<size_t>(h);
    uint8_t* v_buf = u_buf + (static_cast<size_t>(stride_u)) * static_cast<size_t>(h / 2);

    std::vector<uint8_t> u_plane(static_cast<size_t>((w / 2) * (h / 2)));
    std::vector<uint8_t> v_plane(static_cast<size_t>((w / 2) * (h / 2)));

    for (int j = 0; j < h / 2; ++j) {
        std::memcpy(&u_plane[static_cast<size_t>(j * (w / 2))],
                    &u_buf[static_cast<size_t>(j * stride_u)],
                    static_cast<size_t>(w / 2));
        std::memcpy(&v_plane[static_cast<size_t>(j * (w / 2))],
                    &v_buf[static_cast<size_t>(j * stride_u)],
                    static_cast<size_t>(w / 2));
    }

    std::vector<uint8_t> rgb(static_cast<size_t>(w * h * 3));
    yuv420_to_rgb24(y_plane.data(), u_plane.data(), v_plane.data(), w, h, rgb.data());

    auto result = build_raw_video(rgb, w, h, 30, 1, 0);

    decoder->Uninitialize();
    WelsDestroyDecoder(decoder);

    return result;
}

}  // namespace compressor::algorithm