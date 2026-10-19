#ifndef CALC_PRE_INFER_TYPES_HPP
#define CALC_PRE_INFER_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include <opencv2/opencv.hpp>

namespace calc_pre {

enum class PixelFormat {
    Unknown = 0,
    Bgr888,
    Rgb888,
    Nv12,
    Nv21,
    Yuv420p,
};

const char* PixelFormatName(PixelFormat fmt);

struct DmaFrame {
    int fd = -1;
    // 虚拟地址（fallback 路径用 gst_buffer_map 填充；fd=-1 时有效）。
    void* ptr = nullptr;
    int width = 0;
    int height = 0;
    int width_stride = 0;
    int height_stride = 0;
    PixelFormat format = PixelFormat::Unknown;
    std::size_t size = 0;
    // 源端持有的 buffer 索引。消费方用完后必须调用
    // VideoSource::release_frame(buffer_index)。-1 表示无持有。
    int buffer_index = -1;

    bool valid() const {
        return (fd >= 0 || ptr != nullptr) && width > 0 && height > 0 &&
               width_stride > 0 && height_stride > 0;
    }
};

struct LetterboxMeta {
    int source_width = 0;
    int source_height = 0;
    int input_width = 0;
    int input_height = 0;
    float scale = 1.0f;
    int resized_width = 0;
    int resized_height = 0;
    int pad_x = 0;
    int pad_y = 0;
    bool valid = false;
    bool map_to_source = false;
};

struct FrameMeta {
    int frame_index = 0;
    int64_t pts_us = 0;
    double source_fps = 0.0;
    std::string source_id;
};

struct RawFrame {
    FrameMeta meta;
    DmaFrame dma;
    cv::Mat bgr;   // 生产路径保持为空

    bool empty() const {
        return !dma.valid() && bgr.empty();
    }
};

struct PreInferFrame {
    FrameMeta meta;
    cv::Mat source_bgr;        // RGA NV12→BGR 产物 (抓拍/疲劳/裁剪用)
    LetterboxMeta letterbox;
    DmaFrame source_dma;

    bool valid() const {
        return source_dma.valid() && letterbox.valid;
    }
};

struct SourceInfo {
    int width = 0;
    int height = 0;
    double fps = 0.0;
    std::string description;
};

}  // namespace calc_pre

#endif  // CALC_PRE_INFER_TYPES_HPP
