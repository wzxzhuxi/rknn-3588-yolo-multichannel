#ifndef CALC_APP_DMA_FRAME_HPP
#define CALC_APP_DMA_FRAME_HPP

#include <cstddef>
#include <cstdint>

#include <opencv2/core/mat.hpp>

namespace app {

enum class DmaPixelFormat {
    Unknown = 0,
    Bgr888,
    Rgb888,
    Nv12,
    Nv21,
    Yuv420p,
};

// DmaFrame describes one decoded hardware frame. The fd ownership stays with
// the decoder frame object; users must finish any RGA work before releasing it.
struct DmaFrame {
    int fd = -1;
    // 虚拟地址（fallback 路径填充；fd=-1 时有效）。
    void* ptr = nullptr;
    int width = 0;
    int height = 0;
    int width_stride = 0;
    int height_stride = 0;
    DmaPixelFormat format = DmaPixelFormat::Unknown;
    std::size_t size = 0;

    bool valid() const {
        return (fd >= 0 || ptr != nullptr) && width > 0 && height > 0 && width_stride > 0;
    }
};

struct LetterboxMeta {
    int source_width = 0;
    int source_height = 0;
    int input_width = 0;
    int input_height = 0;
    float ratio_x = 1.0f;
    float ratio_y = 1.0f;
    int resized_width = 0;
    int resized_height = 0;
    int pad_x = 0;
    int pad_y = 0;
    bool valid = false;
    bool map_to_source = false;
};

struct PreprocessedFrame {
    LetterboxMeta letterbox;
    DmaFrame source_dma;
    cv::Mat source_bgr;  // CPU fallback when DMA/RGA path is unavailable

    bool valid() const {
        return source_dma.valid() || !source_bgr.empty();
    }
};

}  // namespace app

#endif  // CALC_APP_DMA_FRAME_HPP
