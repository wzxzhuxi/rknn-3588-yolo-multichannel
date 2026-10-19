#ifndef CALC_POST_INFER_POST_CONFIG_HPP
#define CALC_POST_INFER_POST_CONFIG_HPP

#include <string>

namespace calc_post {

struct PostInferConfig {
    bool enable_tracking = true;
    float track_iou_thres = 0.2f;
    int track_max_age = 30;

    // false: 关闭快照管线 (纯检测场景省 CPU/RGA/JPEG)
    bool enable_snapshot = true;
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

}  // namespace calc_post

#endif  // CALC_POST_INFER_POST_CONFIG_HPP
