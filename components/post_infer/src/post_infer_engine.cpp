#include "calc_post_infer/post_infer_engine.hpp"

#include "calc_post_infer/roi_mapper.hpp"

namespace calc_post {

PostInferEngine::PostInferEngine(PostInferConfig config) : cfg_(std::move(config)) {}

PostInferEngine::~PostInferEngine() = default;

bool PostInferEngine::init(std::string* err) {
    if (cfg_.enable_tracking) {
        tracker_ = std::make_unique<PersonTracker>(cfg_.track_iou_thres, cfg_.track_max_age);
    }

    if (cfg_.enable_snapshot) {
        snapshot_manager_ = std::make_unique<SnapshotManager>(cfg_);
        if (!snapshot_manager_->init(err)) {
            return false;
        }
    }
    ready_ = true;
    return true;
}

bool PostInferEngine::process(const PostInferInput& input, PostInferResult* output, std::string* err) {
    if (!ready_) {
        if (err) *err = "PostInferEngine is not initialized";
        return false;
    }
    if (!output) {
        if (err) *err = "null output pointer";
        return false;
    }

    output->detections = input.infer.detections;
    output->tracks.clear();
    output->snapshots.clear();

    if (tracker_) {
        tracker_->update(&output->detections);
    }

    const int width = input.infer.letterbox.source_width > 0 ? input.infer.letterbox.source_width
                                                             : (input.source_bgr.empty() ? 0 : input.source_bgr.cols);
    const int height = input.infer.letterbox.source_height > 0 ? input.infer.letterbox.source_height
                                                               : (input.source_bgr.empty() ? 0 : input.source_bgr.rows);

    for (const auto& det : output->detections) {
        TrackOutput track;
        track.track_id = det.track_id;
        track.source_roi = ClampRect(det.bbox, width, height);
        if (track.source_roi.empty()) {
            track.source_roi = det.bbox;
        }
        track.det_score = det.score;
        output->tracks.push_back(std::move(track));
    }

    if (snapshot_manager_ && !input.source_bgr.empty()) {
        if (!snapshot_manager_->processFrame(input.infer.meta.frame_index, output->detections, input.source_bgr,
                                             &output->snapshots, err)) {
            return false;
        }
    }
    return true;
}

bool PostInferEngine::flush(std::string* err) {
    if (!ready_) {
        if (err) *err = "PostInferEngine is not initialized";
        return false;
    }
    if (!snapshot_manager_) return true;
    std::vector<SnapshotRecord> snapshots;
    return snapshot_manager_->flushDisappeared(&snapshots, err);
}

}  // namespace calc_post
