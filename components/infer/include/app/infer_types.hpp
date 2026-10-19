#ifndef CALC_INFER_TYPES_HPP
#define CALC_INFER_TYPES_HPP

#include <cstdint>
#include <vector>

#include <opencv2/opencv.hpp>

#include "app/dma_frame.hpp"
#include "app/types.hpp"

namespace app {

struct InferFrameMeta {
    int frame_index = 0;
    int64_t pts_us = 0;
    double source_fps = 0.0;
};

struct InferLetterbox {
    int source_width = 0;
    int source_height = 0;
    int input_width = 0;
    int input_height = 0;
    float ratio_x = 1.0f;
    float ratio_y = 1.0f;
    int pad_x = 0;
    int pad_y = 0;
    bool valid = false;
};

struct InferInputFrame {
    InferFrameMeta meta;

    // Required detector input from calc_pre.
    DmaFrame detector_source_dma;
    InferLetterbox letterbox;

    // Optional source image for snapshots. It is not part of the detector input path.
    cv::Mat source_bgr;

    bool valid() const { return detector_source_dma.valid(); }
};

struct InferTimings {
    double detect_ms = 0.0;
};

struct InferResultFrame {
    InferFrameMeta meta;
    InferLetterbox letterbox;
    std::vector<Detection> detections;  // 源图像素坐标
    InferTimings timings;
};

}  // namespace app

#endif  // CALC_INFER_TYPES_HPP
