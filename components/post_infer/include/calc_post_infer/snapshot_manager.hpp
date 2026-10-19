#ifndef CALC_POST_INFER_SNAPSHOT_MANAGER_HPP
#define CALC_POST_INFER_SNAPSHOT_MANAGER_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <opencv2/core.hpp>

#include "app/types.hpp"
#include "calc_post_infer/post_config.hpp"
#include "calc_post_infer/post_types.hpp"

namespace calc_post {

class SnapshotManager {
  public:
    explicit SnapshotManager(PostInferConfig config);

    bool init(std::string* err);

    bool processFrame(int frame_index, const std::vector<app::Detection>& detections, const cv::Mat& source_bgr,
                      std::vector<SnapshotRecord>* snapshots, std::string* err);

    bool flushDisappeared(std::vector<SnapshotRecord>* snapshots, std::string* err);

    bool cleanupCache(std::string* err);

  private:
    struct PendingSnapshot {
        SnapshotRecord record;
        cv::Mat image;
    };

    struct TrackSnapshotState {
        bool has_snapshot = false;
        PendingSnapshot last_snapshot;
        bool has_saved = false;
        int last_saved_frame = 0;
        int last_seen_frame = 0;
    };

    bool buildSnapshot(int frame_index, const cv::Mat& source_bgr, const app::Detection& detection,
                       PendingSnapshot* out, std::string* err) const;

    bool shouldWrite(const TrackSnapshotState& state, int frame_index) const;
    bool writeSnapshot(PendingSnapshot snapshot, std::vector<SnapshotRecord>* snapshots, std::string* err);

    std::string buildImageName(const SnapshotRecord& record) const;

    PostInferConfig cfg_;
    int save_interval_frames_ = 45;
    int disappear_grace_frames_ = 8;
    int64_t next_sequence_ = 0;
    double last_cleanup_ts_ = -1e9;  // 清理节流 (全目录 stat 扫描代价高)
    std::unordered_map<int, TrackSnapshotState> track_states_;
};

}  // namespace calc_post

#endif  // CALC_POST_INFER_SNAPSHOT_MANAGER_HPP
