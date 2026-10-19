#ifndef CALC_POST_INFER_ROI_MAPPER_HPP
#define CALC_POST_INFER_ROI_MAPPER_HPP

#include <opencv2/core.hpp>

namespace calc_post {

cv::Rect ClampRect(const cv::Rect& rect, int width, int height);

cv::Rect ExpandRect(const cv::Rect& rect, int width, int height, double left_ratio, double top_ratio,
                    double right_ratio, double bottom_ratio);

}  // namespace calc_post

#endif  // CALC_POST_INFER_ROI_MAPPER_HPP
