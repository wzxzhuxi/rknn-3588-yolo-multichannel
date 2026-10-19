#include "calc_post_infer/snapshot_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include "calc_post_infer/roi_mapper.hpp"

namespace calc_post {
namespace {

bool IsJpegPath(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".jpg" || ext == ".jpeg";
}

}  // namespace

SnapshotManager::SnapshotManager(PostInferConfig config) : cfg_(std::move(config)) {
    const double fps = cfg_.process_fps > 0.0 ? cfg_.process_fps : 15.0;
    save_interval_frames_ =
        std::max(1, static_cast<int>(std::llround(std::max(0.1, cfg_.snapshot_save_interval_sec) * fps)));
    disappear_grace_frames_ =
        std::max(1, static_cast<int>(std::llround(std::max(0.1, cfg_.snapshot_disappear_grace_sec) * fps)));
}

bool SnapshotManager::init(std::string* err) {
    if (cfg_.snapshot_dir.empty()) {
        if (err) *err = "snapshot_dir is empty";
        return false;
    }

    try {
        std::filesystem::create_directories(cfg_.snapshot_dir);
    } catch (const std::exception& e) {
        if (err) *err = std::string("failed to create snapshot_dir: ") + e.what();
        return false;
    }
    return cleanupCache(err);
}

bool SnapshotManager::processFrame(int frame_index, const std::vector<app::Detection>& detections,
                                   const cv::Mat& source_bgr, std::vector<SnapshotRecord>* snapshots,
                                   std::string* err) {
    if (!snapshots) {
        if (err) *err = "null output pointer";
        return false;
    }

    std::unordered_set<int> visible_tracks;
    for (const auto& det : detections) {
        if (det.track_id < 0) {
            continue;
        }

        PendingSnapshot pending;
        std::string local_err;
        if (!buildSnapshot(frame_index, source_bgr, det, &pending, &local_err)) {
            if (!local_err.empty()) {
                if (err) *err = local_err;
                return false;
            }
            continue;
        }

        auto& state = track_states_[det.track_id];
        state.last_seen_frame = frame_index;
        state.last_snapshot = std::move(pending);
        state.has_snapshot = true;
        visible_tracks.insert(det.track_id);

        if (shouldWrite(state, frame_index)) {
            if (!writeSnapshot(state.last_snapshot, snapshots, err)) {
                return false;
            }
            state.has_saved = true;
            state.last_saved_frame = frame_index;
        }
    }

    if (!cfg_.snapshot_save_on_disappear) {
        return true;
    }

    std::vector<int> stale_tracks;
    for (auto& kv : track_states_) {
        const int track_id = kv.first;
        auto& state = kv.second;
        if (visible_tracks.find(track_id) != visible_tracks.end()) {
            continue;
        }
        if (!state.has_snapshot) {
            stale_tracks.push_back(track_id);
            continue;
        }
        if (frame_index <= state.last_seen_frame + disappear_grace_frames_) {
            continue;
        }
        if (!state.has_saved || state.last_seen_frame > state.last_saved_frame) {
            if (!writeSnapshot(state.last_snapshot, snapshots, err)) {
                return false;
            }
        }
        stale_tracks.push_back(track_id);
    }
    for (int track_id : stale_tracks) {
        track_states_.erase(track_id);
    }
    return true;
}

bool SnapshotManager::flushDisappeared(std::vector<SnapshotRecord>* snapshots, std::string* err) {
    if (!snapshots) {
        if (err) *err = "null output pointer";
        return false;
    }
    for (auto& kv : track_states_) {
        auto& state = kv.second;
        if (!state.has_snapshot) {
            continue;
        }
        if (!state.has_saved || state.last_seen_frame > state.last_saved_frame) {
            if (!writeSnapshot(state.last_snapshot, snapshots, err)) {
                return false;
            }
        }
    }
    track_states_.clear();
    return true;
}

bool SnapshotManager::buildSnapshot(int frame_index, const cv::Mat& source_bgr, const app::Detection& detection,
                                    PendingSnapshot* out, std::string* err) const {
    if (!out) {
        if (err) *err = "null pending snapshot pointer";
        return false;
    }
    if (source_bgr.empty()) {
        if (err) *err = "snapshot requires source_bgr";
        return false;
    }

    cv::Rect source_roi = ClampRect(detection.bbox, source_bgr.cols, source_bgr.rows);
    if (source_roi.empty()) {
        return false;
    }
    if (source_roi.width <= cfg_.snapshot_min_source_width || source_roi.height <= cfg_.snapshot_min_source_height) {
        return false;
    }

    cv::Rect crop_roi =
        ExpandRect(source_roi, source_bgr.cols, source_bgr.rows, cfg_.snapshot_expand_left_ratio,
                   cfg_.snapshot_expand_top_ratio, cfg_.snapshot_expand_right_ratio, cfg_.snapshot_expand_bottom_ratio);
    if (crop_roi.empty()) {
        crop_roi = source_roi;
    }

    SnapshotRecord record;
    record.frame_index = frame_index;
    record.track_id = detection.track_id;
    record.source_roi = source_roi;
    record.crop_roi = crop_roi;
    record.det_score = detection.score;

    out->record = std::move(record);
    out->image = source_bgr(crop_roi).clone();
    return !out->image.empty();
}

bool SnapshotManager::shouldWrite(const TrackSnapshotState& state, int frame_index) const {
    return !state.has_saved || frame_index >= state.last_saved_frame + save_interval_frames_;
}

bool SnapshotManager::writeSnapshot(PendingSnapshot snapshot, std::vector<SnapshotRecord>* snapshots,
                                    std::string* err) {
    if (snapshot.image.empty()) {
        return true;
    }

    snapshot.record.sequence = next_sequence_++;
    snapshot.record.image_path = (std::filesystem::path(cfg_.snapshot_dir) / buildImageName(snapshot.record)).string();

    // CPU JPEG: 单 track 每秒最多一张，开销可忽略；避免 mppjpegenc 的兼容性坑。
    std::vector<int> params = {
        cv::IMWRITE_JPEG_QUALITY,
        std::clamp(cfg_.snapshot_jpeg_quality, 1, 100),
    };
    if (!cv::imwrite(snapshot.record.image_path, snapshot.image, params)) {
        if (err) *err = "failed to write snapshot: " + snapshot.record.image_path;
        return false;
    }

    snapshots->push_back(snapshot.record);
    // 清理是全目录 stat 扫描 (cache 上限可达 2000 文件), 每次保存都跑会
    // 推高 CPU; 节流为 5s 一次.
    const double now_s = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now_s - last_cleanup_ts_ >= 5.0) {
        last_cleanup_ts_ = now_s;
        std::string cleanup_err;
        if (!cleanupCache(&cleanup_err)) {
            std::cerr << "[WARN] snapshot cleanup failed: " << cleanup_err << "\n";
        }
    }
    return true;
}

std::string SnapshotManager::buildImageName(const SnapshotRecord& record) const {
    std::ostringstream name;
    name << "track_" << record.track_id << "_f" << std::setw(8) << std::setfill('0') << record.frame_index << "_i"
         << std::setw(8) << std::setfill('0') << record.sequence << ".jpg";
    return name.str();
}

bool SnapshotManager::cleanupCache(std::string* err) {
    namespace fs = std::filesystem;

    try {
        if (!fs::exists(cfg_.snapshot_dir)) {
            return true;
        }

        struct FileEntry {
            fs::path path;
            fs::file_time_type mtime;
        };
        std::vector<FileEntry> files;
        const auto now = fs::file_time_type::clock::now();
        for (const auto& entry : fs::directory_iterator(cfg_.snapshot_dir)) {
            if (!entry.is_regular_file() || !IsJpegPath(entry.path())) {
                continue;
            }
            std::filesystem::file_time_type mtime;
            try {
                mtime = entry.last_write_time();
            } catch (const std::filesystem::filesystem_error&) {
                continue;  // 文件被并发清理移除 — 跳过即可
            }
            if (cfg_.snapshot_cache_max_age_sec > 0 &&
                now - mtime > std::chrono::seconds(cfg_.snapshot_cache_max_age_sec)) {
                fs::remove(entry.path());
                continue;
            }
            files.push_back({entry.path(), mtime});
        }

        if (cfg_.snapshot_cache_max_files > 0 && static_cast<int>(files.size()) > cfg_.snapshot_cache_max_files) {
            std::sort(files.begin(), files.end(),
                      [](const FileEntry& a, const FileEntry& b) { return a.mtime < b.mtime; });
            const size_t remove_count = files.size() - static_cast<size_t>(cfg_.snapshot_cache_max_files);
            for (size_t i = 0; i < remove_count; ++i) {
                fs::remove(files[i].path);
            }
        }
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }

    return true;
}

}  // namespace calc_post
