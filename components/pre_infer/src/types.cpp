#include "calc_pre_infer/types.hpp"

namespace calc_pre {

const char* PixelFormatName(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::Bgr888:  return "bgr888";
        case PixelFormat::Rgb888:  return "rgb888";
        case PixelFormat::Nv12:    return "nv12";
        case PixelFormat::Nv21:    return "nv21";
        case PixelFormat::Yuv420p: return "yuv420p";
        default:                   return "unknown";
    }
}

}  // namespace calc_pre
