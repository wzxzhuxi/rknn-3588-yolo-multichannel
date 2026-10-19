#include "calc_common_config/project_adapters.hpp"

namespace calc_config {

calc_pre::PreInferConfig ToPreInferConfig(const AppConfig& cfg, const ChannelConfig& ch) {
    calc_pre::PreInferConfig out;
    out.input_mode = cfg.video.input_mode;
    out.source_id = ch.source_id.empty() ? ch.name : ch.source_id;
    out.stream_url = ChannelStreamUrl(cfg, ch);
    out.codec = ch.codec;
    out.source_width = cfg.video.source_width;
    out.source_height = cfg.video.source_height;
    out.detector_width = cfg.video.detector_width;
    out.detector_height = cfg.video.detector_height;
    out.letterbox = cfg.video.letterbox;
    out.map_to_source = cfg.video.map_to_source;
    out.keep_source_bgr = cfg.video.keep_source_bgr;
    out.prefer_rga = cfg.video.prefer_rga;
    out.require_rga = cfg.video.require_rga;
    out.target_fps = ChannelTargetFps(cfg, ch);
    out.log_every = cfg.video.log_every;
    return out;
}

app::AppConfig ToInferConfig(const AppConfig& cfg) {
    app::AppConfig out;
    out.yolo_model = cfg.infer.yolo_model;
    out.conf_thres = cfg.infer.conf_thres;
    out.iou_thres = cfg.infer.iou_thres;
    out.letterbox = cfg.video.letterbox;
    out.yolo_core_mask = cfg.infer.yolo_core_mask;
    out.log_every = cfg.infer.log_every;
    return out;
}

calc_post::PostInferConfig ToPostInferConfig(const AppConfig& cfg) {
    calc_post::PostInferConfig out;
    out.enable_tracking = cfg.post.enable_tracking;
    out.track_iou_thres = cfg.post.track_iou_thres;
    out.track_max_age = cfg.post.track_max_age;
    out.enable_snapshot = cfg.post.enable_snapshot;
    out.process_fps = cfg.post.process_fps;
    out.snapshot_save_interval_sec = cfg.post.snapshot_save_interval_sec;
    out.snapshot_save_on_disappear = cfg.post.snapshot_save_on_disappear;
    out.snapshot_disappear_grace_sec = cfg.post.snapshot_disappear_grace_sec;
    out.snapshot_min_source_width = cfg.post.snapshot_min_source_width;
    out.snapshot_min_source_height = cfg.post.snapshot_min_source_height;
    out.snapshot_expand_left_ratio = cfg.post.snapshot_expand_left_ratio;
    out.snapshot_expand_top_ratio = cfg.post.snapshot_expand_top_ratio;
    out.snapshot_expand_right_ratio = cfg.post.snapshot_expand_right_ratio;
    out.snapshot_expand_bottom_ratio = cfg.post.snapshot_expand_bottom_ratio;
    out.snapshot_dir = cfg.post.snapshot_dir;
    out.snapshot_jpeg_quality = cfg.post.snapshot_jpeg_quality;
    out.snapshot_cache_max_files = cfg.post.snapshot_cache_max_files;
    out.snapshot_cache_max_age_sec = cfg.post.snapshot_cache_max_age_sec;
    out.log_every = cfg.post.log_every;
    return out;
}

}  // namespace calc_config
