#ifndef CALC_APP_TYPES_HPP
#define CALC_APP_TYPES_HPP

#include <opencv2/core.hpp>

namespace app {

// 检测器输出的最小结果单元，坐标已在源图像素空间。
struct Detection {
    cv::Rect bbox;
    float score = 0.0f;
    int track_id = -1;
};

}  // namespace app

#endif  // CALC_APP_TYPES_HPP
