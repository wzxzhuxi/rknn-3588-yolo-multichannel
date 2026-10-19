#include "app/pre_infer_bridge.hpp"

namespace app {
namespace {

DmaPixelFormat ToInferPixelFormat(calc_pre::PixelFormat fmt) {
    switch (fmt) {
        case calc_pre::PixelFormat::Bgr888: return DmaPixelFormat::Bgr888;
        case calc_pre::PixelFormat::Rgb888: return DmaPixelFormat::Rgb888;
        case calc_pre::PixelFormat::Nv12: return DmaPixelFormat::Nv12;
        case calc_pre::PixelFormat::Nv21: return DmaPixelFormat::Nv21;
        case calc_pre::PixelFormat::Yuv420p: return DmaPixelFormat::Yuv420p;
        case calc_pre::PixelFormat::Unknown:
        default: return DmaPixelFormat::Unknown;
    }
}

DmaFrame ToInferDmaFrame(const calc_pre::DmaFrame& frame) {
    DmaFrame out;
    out.fd = frame.fd;
    out.ptr = frame.ptr;
    out.width = frame.width;
    out.height = frame.height;
    out.width_stride = frame.width_stride;
    out.height_stride = frame.height_stride;
    out.format = ToInferPixelFormat(frame.format);
    out.size = frame.size;
    return out;
}

}  // namespace

InferInputFrame FromPreInferFrame(const calc_pre::PreInferFrame& frame) {
    InferInputFrame out;
    out.meta.frame_index = frame.meta.frame_index;
    out.meta.pts_us = frame.meta.pts_us;
    out.meta.source_fps = frame.meta.source_fps;

    out.detector_source_dma = ToInferDmaFrame(frame.source_dma);
    out.source_bgr = frame.source_bgr;

    out.letterbox.source_width = frame.letterbox.source_width;
    out.letterbox.source_height = frame.letterbox.source_height;
    out.letterbox.input_width = frame.letterbox.input_width;
    out.letterbox.input_height = frame.letterbox.input_height;
    out.letterbox.ratio_x = frame.letterbox.scale;
    out.letterbox.ratio_y = frame.letterbox.scale;
    out.letterbox.pad_x = frame.letterbox.pad_x;
    out.letterbox.pad_y = frame.letterbox.pad_y;
    out.letterbox.valid = frame.letterbox.valid;
    return out;
}

}  // namespace app
