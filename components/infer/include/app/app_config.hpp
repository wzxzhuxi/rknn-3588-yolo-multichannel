#ifndef CALC_INFER_APP_CONFIG_HPP
#define CALC_INFER_APP_CONFIG_HPP

#include <string>

namespace app {

struct AppConfig {
    std::string yolo_model = "models/yolov5s_relu_pt.rknn";

    float conf_thres = 0.35f;
    float iou_thres = 0.45f;
    bool letterbox = true;

    std::string yolo_core_mask = "auto";

    int log_every = 30;
};

}  // namespace app

#endif  // CALC_INFER_APP_CONFIG_HPP
