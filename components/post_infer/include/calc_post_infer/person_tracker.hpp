#ifndef CALC_POST_INFER_PERSON_TRACKER_HPP
#define CALC_POST_INFER_PERSON_TRACKER_HPP

#include <vector>

#include <opencv2/core.hpp>

#include "app/types.hpp"

namespace calc_post {

struct TrackState {
    int id = -1;
    cv::Rect bbox;
    int missed = 0;
};

// 纯 IoU 跟踪：没有 embedding，人形框关联只靠空间重叠。
class PersonTracker {
  public:
    PersonTracker(float iou_thres, int max_age);

    void update(std::vector<app::Detection>* detections);
    const std::vector<TrackState>& tracks() const { return tracks_; }

  private:
    float iou_thres_ = 0.2f;
    int max_age_ = 30;
    int next_id_ = 0;
    std::vector<TrackState> tracks_;
};

float IoU(const cv::Rect& a, const cv::Rect& b);

}  // namespace calc_post

#endif  // CALC_POST_INFER_PERSON_TRACKER_HPP
