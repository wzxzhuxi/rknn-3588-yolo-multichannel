#ifndef CALC_COMMON_CONFIG_APP_CONFIG_HPP
#define CALC_COMMON_CONFIG_APP_CONFIG_HPP

#include <map>
#include <string>
#include <vector>

namespace calc_config {

struct VideoConfig {
    std::string input_mode = "rtsp";  // 3588 仅支持 rtsp (vi 保留兼容, 校验拒绝)
    std::string source;
    std::string stream_url = "rtsp://127.0.0.1/live/1";
    std::string source_id = "camera0";

    int source_width = 1280;
    int source_height = 720;

    int detector_width = 640;
    int detector_height = 640;
    bool letterbox = true;
    bool map_to_source = false;
    bool keep_source_bgr = true;
    bool prefer_rga = true;
    bool require_rga = true;

    int log_every = 30;
};

struct InferConfig {
    // YOLOv5s (COCO class 0 = person) 零拷贝检测。
    std::string yolo_model = "models/yolov5s_relu_pt.rknn";

    float conf_thres = 0.35f;
    float iou_thres = 0.45f;

    std::string yolo_core_mask = "auto";

    int log_every = 30;
};

struct PostInferConfig {
    bool enable_tracking = true;
    float track_iou_thres = 0.2f;
    int track_max_age = 30;

    bool enable_snapshot = true;  // false: 关闭快照管线 (纯检测场景)
    double process_fps = 15.0;
    double snapshot_save_interval_sec = 3.0;
    bool snapshot_save_on_disappear = true;
    double snapshot_disappear_grace_sec = 0.5;

    int snapshot_min_source_width = 60;
    int snapshot_min_source_height = 60;
    double snapshot_expand_left_ratio = 0.25;
    double snapshot_expand_top_ratio = 0.45;
    double snapshot_expand_right_ratio = 0.25;
    double snapshot_expand_bottom_ratio = 0.45;

    std::string snapshot_dir = "snapshots";
    int snapshot_jpeg_quality = 95;
    int snapshot_cache_max_files = 2000;
    int snapshot_cache_max_age_sec = 7 * 24 * 3600;

    int log_every = 30;
};

struct RuntimeConfig {
    bool foreground_log = true;
    bool save_log = true;
    std::string log_dir = "run_logs";
    std::string log_level = "info";

    bool enable_gst = false;
    int pipeline_queue = 4;
    int npu_workers = 4;  // 3588: 实测 4 worker 与 6 同吞吐、单算子等待减半 (2026-10-16)
    double target_fps = 15.0;
};

// 一路摄像头通道. channels.list 声明顺序即通道顺序;
// 字段缺省时回落: stream_url→video.stream_url, target_fps→runtime.target_fps,
// source_id→通道名.
struct ChannelConfig {
    std::string name;
    bool enable = true;
    std::string stream_url;
    std::string source_id;
    std::string codec = "h264";  // h264 | h265
    double target_fps = 0.0;
};

struct AppConfig {
    VideoConfig video;
    InferConfig infer;
    PostInferConfig post;
    RuntimeConfig runtime;

    // 通道表. 文件 channels.list / channel.<name>.* 声明; 空 → 加载后合成
    // 单通道 "cam0" (回落 video.stream_url).
    std::vector<ChannelConfig> channels;

    // Unknown keys are retained for forward compatibility and diagnostics.
    std::map<std::string, std::string> extra;
};

bool LoadAppConfig(const std::string& path, AppConfig* cfg, std::string* err);
bool ValidateAppConfig(const AppConfig& cfg, std::vector<std::string>* errors);
std::string SummarizeAppConfig(const AppConfig& cfg);

// 通道字段回落求值
const std::string& ChannelStreamUrl(const AppConfig& cfg, const ChannelConfig& ch);
double ChannelTargetFps(const AppConfig& cfg, const ChannelConfig& ch);

}  // namespace calc_config

#endif  // CALC_COMMON_CONFIG_APP_CONFIG_HPP
