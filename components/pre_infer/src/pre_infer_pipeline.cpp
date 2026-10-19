#include "calc_pre_infer/pre_infer_pipeline.hpp"

#include <iostream>
#include <utility>

namespace calc_pre {

PreInferPipeline::PreInferPipeline(PreInferConfig cfg) : cfg_(std::move(cfg)) {}

PreInferPipeline::~PreInferPipeline() { close(); }

bool PreInferPipeline::open(std::string* err) {
    source_ = CreateVideoSource(cfg_);
    if (!source_) {
        if (err) *err = "failed to create video source";
        return false;
    }
    if (!source_->open(cfg_, err)) {
        return false;
    }
    if (!preprocessor_.init(cfg_, err)) {
        return false;
    }
    return true;
}

bool PreInferPipeline::read(PreInferFrame* frame, std::string* err, bool want_source_bgr) {
    if (!frame) {
        if (err) *err = "null pre-infer frame";
        return false;
    }
    if (!source_) {
        if (err) *err = "pipeline is not open";
        return false;
    }

    RawFrame raw;
    if (!source_->read(&raw)) {
        return false;
    }
    if (!preprocessor_.process(raw, frame, err, want_source_bgr)) {
        // 预处理失败也要把 buffer 还回去, 调用方无需感知部分读恢复.
        source_->release_frame(raw.dma.buffer_index);
        return false;
    }
    // preprocessor.process() 已把 raw.dma 拷进 frame->source_dma;
    // buffer_index 随帧走, 释放责任移交给调用方.
    return true;
}

void PreInferPipeline::release_frame(int buffer_index) {
    if (!source_) return;
    source_->release_frame(buffer_index);
}

SourceInfo PreInferPipeline::sourceInfo() const {
    if (!source_) {
        return {};
    }
    return source_->info();
}

void PreInferPipeline::close() {
    if (source_) {
        source_->close();
        source_.reset();
    }
}

}  // namespace calc_pre
