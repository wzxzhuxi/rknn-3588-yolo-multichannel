#include "calc_pre_infer/preprocessor.hpp"

#include "calc_pre_infer/dma_heap_allocator.hpp"

#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>

#include "im2d.hpp"

namespace calc_pre {
namespace {

LetterboxMeta ComputeLetterbox(int src_w, int src_h, int out_w, int out_h, bool map_to_source) {
    LetterboxMeta meta;
    meta.source_width = src_w;
    meta.source_height = src_h;
    meta.input_width = out_w;
    meta.input_height = out_h;
    const float scale = std::min(static_cast<float>(out_w) / static_cast<float>(src_w),
                                 static_cast<float>(out_h) / static_cast<float>(src_h));
    meta.scale = scale;
    meta.resized_width = std::max(1, static_cast<int>(std::round(src_w * scale)));
    meta.resized_height = std::max(1, static_cast<int>(std::round(src_h * scale)));
    meta.pad_x = (out_w - meta.resized_width) / 2;
    meta.pad_y = (out_h - meta.resized_height) / 2;
    meta.valid = true;
    meta.map_to_source = map_to_source;
    return meta;
}

bool IsRgaOk(IM_STATUS status) { return status == IM_STATUS_SUCCESS || status == IM_STATUS_NOERROR; }

int ToRgaFormat(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::Bgr888: return RK_FORMAT_BGR_888;
        case PixelFormat::Rgb888: return RK_FORMAT_RGB_888;
        case PixelFormat::Nv12: return RK_FORMAT_YCbCr_420_SP;
        case PixelFormat::Nv21: return RK_FORMAT_YCrCb_420_SP;
        default: return 0;
    }
}

}  // namespace

bool Preprocessor::init(const PreInferConfig& cfg, std::string* err) {
    if (cfg.detector_width <= 0 || cfg.detector_height <= 0) {
        if (err) *err = "invalid detector size";
        return false;
    }
    if (!cfg.prefer_rga || !cfg.require_rga) {
        if (err) *err = "zero-copy pre-infer requires prefer_rga=true and require_rga=true";
        return false;
    }
    cfg_ = cfg;
    return true;
}

bool Preprocessor::process(const RawFrame& raw, PreInferFrame* out, std::string* err, bool want_source_bgr) {
    if (!out) {
        if (err) *err = "null output frame";
        return false;
    }
    *out = PreInferFrame{};
    if (raw.empty()) {
        if (err) *err = "empty raw frame";
        return false;
    }
    if (!raw.dma.valid()) {
        if (err) *err = "zero-copy pre-infer requires a valid DMA frame";
        return false;
    }
    return processRgaDma(raw, out, err, want_source_bgr);
}

bool Preprocessor::processRgaDma(const RawFrame& raw, PreInferFrame* out, std::string* err, bool want_source_bgr) {
    const int rga_format = ToRgaFormat(raw.dma.format);
    if (rga_format == 0) {
        if (err) {
            *err = std::string("unsupported RGA input format: ") + PixelFormatName(raw.dma.format);
        }
        return false;
    }

    LetterboxMeta meta =
        ComputeLetterbox(raw.dma.width, raw.dma.height, cfg_.detector_width, cfg_.detector_height, cfg_.map_to_source);
    if (!cfg_.letterbox) {
        meta.scale = static_cast<float>(cfg_.detector_width) / static_cast<float>(raw.dma.width);
        meta.resized_width = cfg_.detector_width;
        meta.resized_height = cfg_.detector_height;
        meta.pad_x = 0;
        meta.pad_y = 0;
    }

    // 源帧 NV12→BGR (RGA 同步, 一次性转换; 快照/疲劳/裁剪都用它)
    if (cfg_.keep_source_bgr && want_source_bgr) {
        rga_buffer_t src = raw.dma.fd >= 0
                               ? wrapbuffer_fd_t(raw.dma.fd, raw.dma.width, raw.dma.height, raw.dma.width_stride,
                                                 raw.dma.height_stride, rga_format)
                               : wrapbuffer_virtualaddr_t(raw.dma.ptr, raw.dma.width, raw.dma.height,
                                                          raw.dma.width_stride, raw.dma.height_stride, rga_format);
        // RGA 直接写进 source_bgr 自己的内存, 无中间缓冲、无 memcpy。
        // 只是把这块内存的来源指定成 dma_heap(system-dma32) —— 见 DmaHeapMatAllocator。
        out->source_bgr.allocator = DmaHeapMatAllocator::Instance();
        out->source_bgr.create(raw.dma.height, raw.dma.width, CV_8UC3);
        // dst 走 fd (6.12 新驱动对 dma_heap VA 的页表解析不认块映射, 见 Allocator 注释);
        // 无 fd 时 (理论不可达) 退回 VA。
        const int dst_fd = DmaHeapMatAllocator::Instance()->FdFor(out->source_bgr.data);
        rga_buffer_t source_dst = dst_fd >= 0
                                      ? wrapbuffer_fd_t(dst_fd, raw.dma.width, raw.dma.height, raw.dma.width,
                                                        raw.dma.height, RK_FORMAT_BGR_888)
                                      : wrapbuffer_virtualaddr_t(out->source_bgr.data, raw.dma.width, raw.dma.height,
                                                                 raw.dma.width, raw.dma.height, RK_FORMAT_BGR_888);
        rga_buffer_t pat{};
        im_rect src_rect{0, 0, raw.dma.width, raw.dma.height};
        im_rect dst_rect{0, 0, raw.dma.width, raw.dma.height};
        im_rect pat_rect{0, 0, 0, 0};
        // 强制走 RGA3: 本板 RGA2 无 >4G 寻址能力, 而 src(MPP VDEC dma-buf) 可能落在
        // 4G 以上 —— 驱动默认调度到 RGA2 时会报
        //   rga_mm: RGA_MMU unsupported memory larger than 4G!
        //   rga_mm: scheduler core[4] unsupported mm_flag[0x0]!   (core[4]=RGA2_SCHEDULER_CORE0)
        //   rga_mm: src channel map job buffer failed!
        // RGA3 双核有 IOMMU, 可以寻址 4G 以上。
        im_opt_t opt{};
        opt.core = RGA3_SCHEDULER_CORE0 | RGA3_SCHEDULER_CORE1;
        IM_STATUS status = improcess(src, source_dst, pat, src_rect, dst_rect, pat_rect, -1, nullptr, &opt, IM_SYNC);
        if (!IsRgaOk(status)) {
            std::cerr << "[pre_infer] RGA source BGR conversion failed: " << imStrError_t(status)
                      << "; source_bgr disabled for this frame.\n";
            out->source_bgr.release();
        }
    }

    out->meta = raw.meta;
    out->letterbox = meta;
    out->source_dma = raw.dma;
    return true;
}

}  // namespace calc_pre
