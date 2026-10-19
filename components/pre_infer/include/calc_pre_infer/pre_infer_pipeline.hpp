#ifndef CALC_PRE_INFER_PIPELINE_HPP
#define CALC_PRE_INFER_PIPELINE_HPP

#include <memory>
#include <string>

#include "calc_pre_infer/config.hpp"
#include "calc_pre_infer/preprocessor.hpp"
#include "calc_pre_infer/types.hpp"
#include "calc_pre_infer/video_source.hpp"

namespace calc_pre {

class PreInferPipeline {
  public:
    explicit PreInferPipeline(PreInferConfig cfg);
    ~PreInferPipeline();

    bool open(std::string* err);
    // want_source_bgr=false 用于抽帧丢弃: 跳过 NV12→BGR RGA 转换后立即 release_frame.
    bool read(PreInferFrame* frame, std::string* err, bool want_source_bgr = true);
    // 归还源端 buffer (gst_sample_unref)。每次成功 read() 必须恰好调用一次
    // (在整条推理链用完 source_dma 之后)。
    void release_frame(int buffer_index);
    SourceInfo sourceInfo() const;
    void close();

  private:
    PreInferConfig cfg_;
    std::unique_ptr<VideoSource> source_;
    Preprocessor preprocessor_;
};

}  // namespace calc_pre

#endif  // CALC_PRE_INFER_PIPELINE_HPP
