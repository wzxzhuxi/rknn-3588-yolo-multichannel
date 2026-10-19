#include "calc_pre_infer/video_source.hpp"

#include <cmath>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include <gst/allocators/gstdmabuf.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video-info.h>

namespace calc_pre {
namespace {

// RTSP 拉流 + MPP 硬解, DMABuf 零拷贝视频源:
//   rtspsrc → depay → parse → mppvideodec → [videorate max-rate=target_fps] → appsink
//   (video/x-raw(memory:DMABuf),NV12)
// read() 提取 dmabuf fd 填 RawFrame.dma (ptr=nullptr), sample 挂到 pending_
// 保持引用, release_frame() 时 gst_sample_unref.
// 协商失败 (非 dmabuf memory) 时告警一次并退回 gst_buffer_map 虚拟地址路径,
// 保证 bring-up 不被卡死; 生产环境出现 fallback 说明链路有问题.
class GstRtspDmabufSource : public VideoSource {
  public:
    ~GstRtspDmabufSource() override { close(); }

    bool open(const PreInferConfig& cfg, std::string* err) override {
        gst_init(nullptr, nullptr);

        codec_h265_ = (cfg.codec == "h265");
        // 固定管线字符串 (不含外部输入), URL 用 g_object_set 单独设置, 避免注入.
        // gst_parse_launch 自动处理 rtspsrc 动态 pad.
        // target_fps>0: videorate max-rate 定频 (implies drop-only, 不重复帧).
        // 抽帧在管线内完成 → 送检率 = min(源率, target), 与源帧率波动解耦;
        // 源慢于 target 时全量通过 (旧序号抽帧会相对源率继续打折, 已废弃).
        const int cap_fps = cfg.target_fps > 0.0 ? static_cast<int>(std::lround(cfg.target_fps)) : 0;
        char desc[640];
        if (cap_fps > 0) {
            std::snprintf(desc, sizeof(desc),
                          "rtspsrc name=src latency=0 protocols=tcp ! %s ! %s ! mppvideodec ! "
                          "videorate max-rate=%d ! appsink name=sink caps=video/x-raw,format=NV12",
                          codec_h265_ ? "rtph265depay" : "rtph264depay", codec_h265_ ? "h265parse" : "h264parse",
                          cap_fps);
        } else {
            std::snprintf(desc, sizeof(desc),
                          "rtspsrc name=src latency=0 protocols=tcp ! %s ! %s ! mppvideodec ! "
                          "appsink name=sink caps=video/x-raw,format=NV12",
                          codec_h265_ ? "rtph265depay" : "rtph264depay", codec_h265_ ? "h265parse" : "h264parse");
        }
        pipeline_ = gst_parse_launch(desc, nullptr);
        if (!pipeline_) {
            if (err) *err = "gst_parse_launch failed";
            return false;
        }

        GstElement* src = gst_bin_get_by_name(GST_BIN(pipeline_), "src");
        GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
        if (!src || !sink) {
            if (err) *err = "gst: src/sink not found";
            close();
            return false;
        }

        g_object_set(src, "location", cfg.stream_url.c_str(), nullptr);
        // 限流: 消费慢时缓存 8 帧, 满则丢最旧 (drop=drop-oldest) — 通道卡死绝不反压 RTSP 会话.
        g_object_set(sink, "sync", FALSE, "max-buffers", 8, "drop", TRUE, nullptr);
        appsink_ = GST_APP_SINK(sink);
        source_id_ = cfg.source_id;

        if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            if (err) *err = "gst pipeline failed to start";
            close();
            return false;
        }
        return true;
    }

    bool read(RawFrame* frame) override {
        if (!frame || !appsink_) return false;

        GstSample* sample = gst_app_sink_try_pull_sample(appsink_, 5 * GST_SECOND);
        if (!sample) return false;

        GstBuffer* buffer = gst_sample_get_buffer(sample);
        if (!buffer) {
            gst_sample_unref(sample);
            return false;
        }

        int width = 0, height = 0, wstride = 0, hstride = 0;
        GstVideoMeta* vmeta = gst_buffer_get_video_meta(buffer);
        if (vmeta) {
            width = vmeta->width;
            height = vmeta->height;
            wstride = vmeta->stride[0];
            // UV 平面偏移换算高度 stride (MPP 对齐, 绝不假设 == height)
            if (vmeta->n_planes >= 2 && wstride > 0 && vmeta->offset[1] > 0) {
                hstride = static_cast<int>(vmeta->offset[1] / vmeta->stride[0]);
            } else {
                hstride = height;
            }
        } else {
            // 无 video meta (meta 未协商): 取 caps 尺寸, stride 按 width 处理并告警.
            GstCaps* caps = gst_sample_get_caps(sample);
            GstStructure* s = caps ? gst_caps_get_structure(caps, 0) : nullptr;
            if (s) {
                gst_structure_get_int(s, "width", &width);
                gst_structure_get_int(s, "height", &height);
            }
            wstride = width;
            hstride = height;
            if (!vmeta_warned_) {
                std::cerr << "[gst_dmabuf] no GstVideoMeta on buffer; assuming stride==width (" << width << ")\n";
                vmeta_warned_ = true;
            }
        }
        if (width <= 0 || height <= 0) {
            std::cerr << "[gst_dmabuf] bad frame size=" << width << "x" << height << "\n";
            gst_sample_unref(sample);
            return false;
        }

        int idx;
        PendingBuf* pb;
        {
            // pending_ 同时被本线程 (read 插入) 和 NPU worker (release_frame 移除) 访问
            std::lock_guard<std::mutex> lk(pending_mu_);
            idx = next_index_++;
            pb = &pending_[idx];
            pb->sample = sample;
            pb->buffer = buffer;
        }
        // 节点式 map: 引用跨 rehash 稳定; 本 idx 在 return 前不会被其他线程 release

        GstMemory* mem = gst_buffer_peek_memory(buffer, 0);
        if (mem && gst_is_dmabuf_memory(mem) && mem->offset == 0) {
            frame->dma.fd = gst_dmabuf_memory_get_fd(mem);
            frame->dma.ptr = nullptr;
            frame->dma.size = mem->size;
            dmabuf_frames_++;
        } else {
            // 协商失败 fallback: 虚拟地址 (非零拷贝, 仅保 bring-up)
            if (!fallback_warned_) {
                std::cerr << "[gst_dmabuf] WARNING: buffer memory is not zero-offset DMABuf"
                             " (negotiation failed?) — falling back to gst_buffer_map"
                             " virtual-address path (NOT zero-copy)\n";
                fallback_warned_ = true;
            }
            if (!gst_buffer_map(buffer, &pb->map, GST_MAP_READ)) {
                {
                    std::lock_guard<std::mutex> lk(pending_mu_);
                    pending_.erase(idx);
                }
                gst_sample_unref(sample);
                return false;
            }
            pb->mapped = true;
            frame->dma.fd = -1;
            frame->dma.ptr = pb->map.data;
            frame->dma.size = pb->map.size;
        }

        width_ = width;
        height_ = height;

        frame->dma.width = width;
        frame->dma.height = height;
        frame->dma.width_stride = wstride;
        frame->dma.height_stride = hstride;
        frame->dma.format = PixelFormat::Nv12;
        frame->dma.buffer_index = idx;

        frame->meta.frame_index = frame_counter_++;
        if (GST_BUFFER_PTS_IS_VALID(buffer)) {
            frame->meta.pts_us = GST_BUFFER_PTS(buffer) / 1000;  // ns -> us
        }
        frame->meta.source_id = source_id_;
        frame->meta.source_fps = 30.0;
        return true;
    }

    void release_frame(int buffer_index) override {
        PendingBuf pb;
        {
            std::lock_guard<std::mutex> lk(pending_mu_);
            auto it = pending_.find(buffer_index);
            if (it == pending_.end()) return;
            pb = it->second;
            pending_.erase(it);
        }
        if (pb.mapped) {
            gst_buffer_unmap(pb.buffer, &pb.map);
        }
        if (pb.sample) gst_sample_unref(pb.sample);
    }

    SourceInfo info() const override {
        SourceInfo si;
        si.width = width_;
        si.height = height_;
        si.fps = 30.0;
        si.description = std::string("rtsp-mpp-decode-") + (fallback_warned_ ? "vaddr" : "dmabuf") + "-nv12";
        return si;
    }

    void close() override {
        if (pipeline_) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            gst_object_unref(pipeline_);
            pipeline_ = nullptr;
            appsink_ = nullptr;
        }
        std::unordered_map<int, PendingBuf> pend;
        {
            std::lock_guard<std::mutex> lk(pending_mu_);
            pend.swap(pending_);
        }
        for (auto& kv : pend) {
            if (kv.second.mapped) {
                gst_buffer_unmap(kv.second.buffer, &kv.second.map);
            }
            if (kv.second.sample) gst_sample_unref(kv.second.sample);
        }
    }

  private:
    struct PendingBuf {
        GstSample* sample = nullptr;
        GstBuffer* buffer = nullptr;
        GstMapInfo map{};
        bool mapped = false;
    };

    GstElement* pipeline_ = nullptr;
    GstAppSink* appsink_ = nullptr;
    std::mutex pending_mu_;  // 保护 pending_ / next_index_
    std::unordered_map<int, PendingBuf> pending_;
    std::string source_id_;
    bool codec_h265_ = false;
    bool fallback_warned_ = false;
    bool vmeta_warned_ = false;
    int next_index_ = 0;
    int frame_counter_ = 0;
    int width_ = 0, height_ = 0;

  public:
    int dmabuf_frames_ = 0;  // 冒烟统计: dmabuf 路径帧数
};

}  // namespace

std::unique_ptr<VideoSource> CreateGstRtspDmabufSource() { return std::make_unique<GstRtspDmabufSource>(); }

std::unique_ptr<VideoSource> CreateVideoSource(const PreInferConfig& cfg) {
    if (cfg.input_mode == "rtsp") {
        return CreateGstRtspDmabufSource();
    }
    return nullptr;
}

}  // namespace calc_pre
