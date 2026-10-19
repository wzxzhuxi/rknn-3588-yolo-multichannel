#ifndef CALC_PRE_INFER_PREPROCESSOR_HPP
#define CALC_PRE_INFER_PREPROCESSOR_HPP

#include <string>

#include "calc_pre_infer/config.hpp"
#include "calc_pre_infer/types.hpp"

namespace calc_pre {

// 预处理: letterbox meta 计算 + (keep_source_bgr 时) RGA NV12→BGR 源帧转换.
// letterbox 缩放在检测器内部完成。
class Preprocessor {
  public:
    bool init(const PreInferConfig& cfg, std::string* err);
    // want_source_bgr=false 时跳过 NV12→BGR RGA 转换 (抽帧丢弃场景, 省 RGA 与 DDR 写带宽);
    // 此时 out->source_bgr 为空, 仅 meta/source_dma 有效.
    bool process(const RawFrame& raw, PreInferFrame* out, std::string* err, bool want_source_bgr = true);

  private:
    bool processRgaDma(const RawFrame& raw, PreInferFrame* out, std::string* err, bool want_source_bgr);

    PreInferConfig cfg_;
};

}  // namespace calc_pre

#endif  // CALC_PRE_INFER_PREPROCESSOR_HPP
