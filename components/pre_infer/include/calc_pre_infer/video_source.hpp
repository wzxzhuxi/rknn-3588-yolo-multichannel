#ifndef CALC_PRE_INFER_VIDEO_SOURCE_HPP
#define CALC_PRE_INFER_VIDEO_SOURCE_HPP

#include <memory>
#include <string>

#include "calc_pre_infer/config.hpp"
#include "calc_pre_infer/types.hpp"

namespace calc_pre {

class VideoSource {
public:
    virtual ~VideoSource() = default;

    virtual bool open(const PreInferConfig& cfg, std::string* err) = 0;
    virtual bool read(RawFrame* frame) = 0;
    // 归还 buffer (gst_sample_unref)。每次成功 read() 必须恰好调用一次。
    // 传 -1 为 no-op。
    virtual void release_frame(int buffer_index) = 0;
    virtual SourceInfo info() const = 0;
    virtual void close() = 0;
};

// 3588 生产输入: RTSP 拉流 + MPP 硬解, 优先 DMABuf fd 零拷贝.
std::unique_ptr<VideoSource> CreateGstRtspDmabufSource();

std::unique_ptr<VideoSource> CreateVideoSource(const PreInferConfig& cfg);

}  // namespace calc_pre

#endif  // CALC_PRE_INFER_VIDEO_SOURCE_HPP
