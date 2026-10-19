#include "calc_post_infer/person_tracker.hpp"

#include <algorithm>

namespace calc_post {

float IoU(const cv::Rect& a, const cv::Rect& b) {
    const int x0 = std::max(a.x, b.x);
    const int y0 = std::max(a.y, b.y);
    const int x1 = std::min(a.x + a.width, b.x + b.width);
    const int y1 = std::min(a.y + a.height, b.y + b.height);
    const int w = std::max(0, x1 - x0);
    const int h = std::max(0, y1 - y0);
    const double inter = static_cast<double>(w) * static_cast<double>(h);
    const double uni = static_cast<double>(a.area()) + static_cast<double>(b.area()) - inter;
    if (uni <= 0.0) {
        return 0.0f;
    }
    return static_cast<float>(inter / uni);
}

PersonTracker::PersonTracker(float iou_thres, int max_age) : iou_thres_(iou_thres), max_age_(std::max(1, max_age)) {}

void PersonTracker::update(std::vector<app::Detection>* detections) {
    if (!detections) {
        return;
    }

    std::vector<int> det_to_track(detections->size(), -1);
    std::vector<int> track_to_det(tracks_.size(), -1);

    struct Pair {
        int det = -1;
        int trk = -1;
        float iou = 0.0f;
    };

    std::vector<Pair> pairs;
    pairs.reserve(detections->size() * tracks_.size());
    for (size_t i = 0; i < detections->size(); ++i) {
        for (size_t j = 0; j < tracks_.size(); ++j) {
            const float iou = IoU((*detections)[i].bbox, tracks_[j].bbox);
            if (iou_thres_ > 0.0f && iou >= iou_thres_) {
                pairs.push_back({static_cast<int>(i), static_cast<int>(j), iou});
            }
        }
    }

    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.iou > b.iou; });

    for (const auto& p : pairs) {
        if (det_to_track[p.det] != -1 || track_to_det[p.trk] != -1) {
            continue;
        }
        det_to_track[p.det] = p.trk;
        track_to_det[p.trk] = p.det;
    }

    for (size_t i = 0; i < detections->size(); ++i) {
        const int trk_idx = det_to_track[i];
        if (trk_idx < 0) {
            continue;
        }
        auto& trk = tracks_[trk_idx];
        auto& det = (*detections)[i];
        trk.bbox = det.bbox;
        trk.missed = 0;
        det.track_id = trk.id;
    }

    for (size_t j = 0; j < tracks_.size(); ++j) {
        if (track_to_det[j] == -1) {
            ++tracks_[j].missed;
        }
    }

    std::vector<TrackState> kept;
    kept.reserve(tracks_.size());
    for (auto& trk : tracks_) {
        if (trk.missed <= max_age_) {
            kept.push_back(std::move(trk));
        }
    }
    tracks_.swap(kept);

    for (size_t i = 0; i < detections->size(); ++i) {
        if (det_to_track[i] != -1) {
            continue;
        }
        TrackState trk;
        trk.id = next_id_++;
        trk.bbox = (*detections)[i].bbox;
        trk.missed = 0;
        (*detections)[i].track_id = trk.id;
        tracks_.push_back(std::move(trk));
    }
}

}  // namespace calc_post
