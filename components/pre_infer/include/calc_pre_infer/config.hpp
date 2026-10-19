#ifndef CALC_PRE_INFER_CONFIG_HPP
#define CALC_PRE_INFER_CONFIG_HPP

#include <string>

namespace calc_pre {

struct PreInferConfig {
    std::string input_mode = "rtsp";
    std::string source_id = "camera0";
    std::string stream_url = "rtsp://127.0.0.1:8554/live/0";
    std::string codec = "h264";  // h264 | h265

    int source_width = 1280;
    int source_height = 720;

    int detector_width = 640;
    int detector_height = 640;
    bool letterbox = true;
    bool map_to_source = false;
    bool keep_source_bgr = true;

    // 3588 契约: DMA fd -> RGA, 无 CPU 回退路径
    bool prefer_rga = true;
    bool require_rga = true;

    bool enable_lowlight = false;

    // GStreamer 内抽帧定频: >0 时在 mppvideodec 后插 `videorate max-rate=N`
    // (implies drop-only — 源快于 N 丢帧、慢于 N 原样通过), 每路送检率与源
    // 帧率解耦; 应用侧不再做序号抽帧. 0 = 不插 (兼容口径).
    double target_fps = 0.0;

    int log_every = 30;
};

}  // namespace calc_pre

#endif  // CALC_PRE_INFER_CONFIG_HPP
