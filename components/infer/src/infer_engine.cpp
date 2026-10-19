#include "app/infer_engine.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <utility>

#include "app/detector.hpp"

#if defined(CALC_INFER_WITH_RKNN)
#include "rknn_api.h"
#endif

namespace app {
namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

int CoreMaskFromString(const std::string& value) {
    const std::string v = Lower(value);
    if (v.empty() || v == "auto" || v == "-1") {
        return -1;
    }
#if defined(CALC_INFER_WITH_RKNN)
    if (v == "0") return RKNN_NPU_CORE_0;
    if (v == "1") return RKNN_NPU_CORE_1;
    if (v == "2") return RKNN_NPU_CORE_2;
    if (v == "01" || v == "0_1") return RKNN_NPU_CORE_0_1;
    if (v == "012" || v == "0_1_2" || v == "all") return RKNN_NPU_CORE_0_1_2;
    return RKNN_NPU_CORE_AUTO;
#else
    return -1;
#endif
}

double NowMs() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

PreprocessedFrame ToDetectorInput(const InferInputFrame& input) {
    PreprocessedFrame out;
    out.letterbox.source_width = input.letterbox.source_width;
    out.letterbox.source_height = input.letterbox.source_height;
    out.letterbox.input_width = input.letterbox.input_width;
    out.letterbox.input_height = input.letterbox.input_height;
    out.letterbox.ratio_x = input.letterbox.ratio_x;
    out.letterbox.ratio_y = input.letterbox.ratio_y;
    if (input.letterbox.valid) {
        out.letterbox.resized_width =
            std::max(1, static_cast<int>(std::round(input.letterbox.source_width * input.letterbox.ratio_x)));
        out.letterbox.resized_height =
            std::max(1, static_cast<int>(std::round(input.letterbox.source_height * input.letterbox.ratio_y)));
    }
    out.letterbox.pad_x = input.letterbox.pad_x;
    out.letterbox.pad_y = input.letterbox.pad_y;
    out.letterbox.valid = input.letterbox.valid;
    out.source_dma = input.detector_source_dma;
    out.source_bgr = input.source_bgr;
    return out;
}

}  // namespace

InferEngine::InferEngine(AppConfig config) : cfg_(std::move(config)) {}

InferEngine::~InferEngine() = default;

bool InferEngine::init(std::string* err) {
    detector_ = CreateYolov5sDetector();
    if (!detector_) {
        if (err) *err = "failed to create YOLOv5s detector";
        return false;
    }

    DetectorOptions options;
    options.conf_thres = cfg_.conf_thres;
    options.iou_thres = cfg_.iou_thres;
    options.letterbox = cfg_.letterbox;
    options.core_mask = CoreMaskFromString(cfg_.yolo_core_mask);
    std::cout << "[infer] yolo_core_mask='" << cfg_.yolo_core_mask << "'->" << options.core_mask << "\n";

    if (!detector_->init(cfg_.yolo_model, options)) {
        if (err) *err = "failed to init detector: " + cfg_.yolo_model;
        return false;
    }
    return true;
}

bool InferEngine::process(const InferInputFrame& input, InferResultFrame* output, std::string* err) {
    if (!output) {
        if (err) *err = "null output";
        return false;
    }
    *output = InferResultFrame{};
    if (!input.valid()) {
        if (err) *err = "invalid infer input frame";
        return false;
    }

    output->meta = input.meta;
    output->letterbox = input.letterbox;

    const double t0 = NowMs();
    output->detections = detector_->detect_preprocessed(ToDetectorInput(input));
    output->timings.detect_ms = NowMs() - t0;
    return true;
}

}  // namespace app
