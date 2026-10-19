#ifndef CALC_POST_INFER_POST_TYPES_HPP
#define CALC_POST_INFER_POST_TYPES_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "app/infer_types.hpp"

namespace calc_post {

struct TrackOutput {
    int track_id = -1;
    cv::Rect source_roi;
    float det_score = 0.0f;
};

struct SnapshotRecord {
    int64_t sequence = 0;
    int frame_index = 0;
    int track_id = -1;
    cv::Rect source_roi;
    cv::Rect crop_roi;
    float det_score = 0.0f;
    std::string image_path;
};

struct PostInferInput {
    app::InferResultFrame infer;

    // Preferred crop source. This should be the original decoded BGR frame
    // preserved by calc_pre_infer.
    cv::Mat source_bgr;
};

struct PostInferResult {
    std::vector<app::Detection> detections;
    std::vector<TrackOutput> tracks;
    std::vector<SnapshotRecord> snapshots;
};

}  // namespace calc_post

#endif  // CALC_POST_INFER_POST_TYPES_HPP
