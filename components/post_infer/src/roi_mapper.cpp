#include "calc_post_infer/roi_mapper.hpp"

#include <algorithm>
#include <cmath>

namespace calc_post {

cv::Rect ClampRect(const cv::Rect& rect, int width, int height) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    const int x = std::max(0, rect.x);
    const int y = std::max(0, rect.y);
    const int right = std::min(width, rect.x + rect.width);
    const int bottom = std::min(height, rect.y + rect.height);
    if (right <= x || bottom <= y) {
        return {};
    }
    return cv::Rect(x, y, right - x, bottom - y);
}

cv::Rect ExpandRect(const cv::Rect& rect, int width, int height, double left_ratio, double top_ratio,
                    double right_ratio, double bottom_ratio) {
    const int left = static_cast<int>(std::lround(rect.width * left_ratio));
    const int top = static_cast<int>(std::lround(rect.height * top_ratio));
    const int right = static_cast<int>(std::lround(rect.width * right_ratio));
    const int bottom = static_cast<int>(std::lround(rect.height * bottom_ratio));
    const int x = std::max(0, rect.x - left);
    const int y = std::max(0, rect.y - top);
    const int x2 = std::min(width, rect.x + rect.width + right);
    const int y2 = std::min(height, rect.y + rect.height + bottom);
    if (x2 <= x || y2 <= y) {
        return {};
    }
    return cv::Rect(x, y, x2 - x, y2 - y);
}

}  // namespace calc_post
