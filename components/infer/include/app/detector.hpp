#ifndef CALC_APP_DETECTOR_HPP
#define CALC_APP_DETECTOR_HPP

#include <memory>
#include <string>
#include <vector>

#include "app/dma_frame.hpp"
#include "app/types.hpp"

namespace app {

// 把检测器实现差异压在实现类内部，Pipeline 只关心阈值、颜色空间和 NPU 核心。
struct DetectorOptions {
    float conf_thres = 0.25f;
    float iou_thres = 0.45f;
    bool letterbox = true;
    int core_mask = -1;
};

// 检测器输入必须是硬件 DMA 帧描述。
class Detector {
  public:
    virtual ~Detector() = default;
    virtual bool init(const std::string& model_path, const DetectorOptions& options) = 0;
    virtual std::vector<Detection> detect_preprocessed(const PreprocessedFrame& input) = 0;
};

// YOLOv5s (COCO person=class 0) 零拷贝检测器。
std::unique_ptr<Detector> CreateYolov5sDetector();

}  // namespace app

#endif  // CALC_APP_DETECTOR_HPP
