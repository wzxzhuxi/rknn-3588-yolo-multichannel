// YOLOv5s (COCO person=class 0) 零拷贝检测器.
// 解码 fd → RGA letterbox → RKNN input io_mem fd → rknn_run → NMS → 源图坐标.
#include "app/detector.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "im2d.hpp"
#include "rknn_api.h"

#include "app/rknn_perf.hpp"  // [npu] 心跳埋点
#include "yolov5s_sub/yolov5_nms.h"

namespace app {
namespace {

std::vector<uint8_t> LoadBinaryFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return {};
    const auto size = in.tellg();
    std::vector<uint8_t> data(static_cast<size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// RGA 驱动映射不了 rknpu 自有分配器的 fd (rga_mm_map_buffer map dma_buf error).
// 从 dma_heap 自分配 (cma 优先, system 兜底), 再用 rknn_create_mem_from_fd 导入.
struct DmaHeapAllocData {
    unsigned long long len;
    unsigned int fd;
    unsigned int fd_flags;
    unsigned long long heap_flags;
};
constexpr unsigned long DMA_HEAP_IOCTL_ALLOC_CMD = 0xc0184800;

int AllocDmaHeap(size_t size) {
    const char* heap_paths[] = {"/dev/dma_heap/cma", "/dev/dma_heap/system", nullptr};
    for (int i = 0; heap_paths[i]; ++i) {
        int heap = open(heap_paths[i], O_RDWR | O_CLOEXEC);
        if (heap < 0) continue;
        DmaHeapAllocData alloc{size, 0, 2, 0};  // fd_flags=2: O_RDWR
        if (ioctl(heap, DMA_HEAP_IOCTL_ALLOC_CMD, &alloc) == 0) {
            close(heap);
            return static_cast<int>(alloc.fd);
        }
        close(heap);
    }
    return -1;
}

bool IsRgaOk(IM_STATUS status) { return status == IM_STATUS_SUCCESS || status == IM_STATUS_NOERROR; }

int ToRgaFormat(DmaPixelFormat fmt) {
    switch (fmt) {
        case DmaPixelFormat::Bgr888: return RK_FORMAT_BGR_888;
        case DmaPixelFormat::Rgb888: return RK_FORMAT_RGB_888;
        case DmaPixelFormat::Nv12: return RK_FORMAT_YCbCr_420_SP;
        case DmaPixelFormat::Nv21: return RK_FORMAT_YCrCb_420_SP;
        default: return 0;
    }
}

class Yolov5sDetector : public Detector {
  public:
    ~Yolov5sDetector() override {
        if (ctx != 0) {
            if (input_mem_) rknn_destroy_mem(ctx, input_mem_);
            rknn_destroy(ctx);
        }
        if (own_va_) munmap(own_va_, own_size_);
        if (own_fd_ >= 0) close(own_fd_);
    }

    bool init(const std::string& model_path, const DetectorOptions& options) override {
        conf_thres_ = options.conf_thres;
        iou_thres_ = options.iou_thres;
        letterbox_ = options.letterbox;

        std::vector<uint8_t> model_data = LoadBinaryFile(model_path);
        if (model_data.empty()) {
            std::cerr << "[yolo] failed to load model: " << model_path << "\n";
            return false;
        }
        int ret = rknn_init(&ctx, model_data.data(), static_cast<uint32_t>(model_data.size()), 0, nullptr);
        if (ret != RKNN_SUCC) {
            std::cerr << "[yolo] rknn_init failed: " << ret << "\n";
            return false;
        }
        if (options.core_mask >= 0) {
            rknn_set_core_mask(ctx, static_cast<rknn_core_mask>(options.core_mask));
        }

        rknn_input_output_num io_num{};
        ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
        if (ret != RKNN_SUCC) return false;

        rknn_tensor_attr input_attr{};
        input_attr.index = 0;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attr, sizeof(input_attr));
        if (ret != RKNN_SUCC) return false;
        // NHWC: dims = [1, h, w, c]
        input_h_ = static_cast<int>(input_attr.dims[1]);
        input_w_ = static_cast<int>(input_attr.dims[2]);
        input_c_ = static_cast<int>(input_attr.dims[3]);
        std::cout << "[yolo] input " << input_w_ << "x" << input_h_ << "x" << input_c_ << " type=" << input_attr.type
                  << " fmt=" << input_attr.fmt << "\n";

        output_attrs_.resize(io_num.n_output);
        for (uint32_t i = 0; i < io_num.n_output; ++i) {
            rknn_tensor_attr attr{};
            attr.index = i;
            ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr));
            if (ret != RKNN_SUCC) return false;
            output_attrs_[i] = attr;
            out_zps_.push_back(attr.zp);
            out_scales_.push_back(attr.scale);
        }
        // 头排序: 输出布局 [1, 255, grid, grid] (255 = 3 anchor × 85 prop, NCHW).
        // 按 dims[2] (grid 边长 80/40/20) 降序确定 input0/1/2 顺序, 不依赖默认输出序.
        std::sort(head_order_.begin(), head_order_.end(),
                  [&](int a, int b) { return output_attrs_[a].dims[2] > output_attrs_[b].dims[2]; });
        for (int i = 0; i < 3; ++i) {
            const auto& a = output_attrs_[head_order_[i]];
            std::cout << "[yolo] head" << i << " grid=" << a.dims[2] << " ch=" << a.dims[1] << " zp=" << a.zp
                      << " scale=" << a.scale << "\n";
        }
        return initInputIoMem(input_attr);
    }

    std::vector<Detection> detect_preprocessed(const PreprocessedFrame& input) override {
        std::vector<Detection> detections;
        if (!runPreparedDma(input)) return detections;

        std::vector<rknn_output> outputs(output_attrs_.size());
        for (size_t i = 0; i < outputs.size(); ++i) {
            outputs[i].want_float = 0;
            outputs[i].is_prealloc = 0;
            outputs[i].index = static_cast<uint32_t>(i);
        }
        int ret = rknn_outputs_get(ctx, static_cast<uint32_t>(outputs.size()), outputs.data(), nullptr);
        if (ret != RKNN_SUCC) {
            std::cerr << "[yolo] rknn_outputs_get failed: " << ret << "\n";
            return detections;
        }

        yolov5::detect_result_group_t group{};
        std::vector<int32_t> zps;
        std::vector<float> scales;
        int8_t* head_ptr[3];
        for (int i = 0; i < 3; ++i) {
            head_ptr[i] = static_cast<int8_t*>(outputs[head_order_[i]].buf);
            zps.push_back(output_attrs_[head_order_[i]].zp);
            scales.push_back(output_attrs_[head_order_[i]].scale);
        }
        // scale=1.0: NMS 输出模型 640 空间坐标, letterbox 映射在下方手动做.
        yolov5::post_process(head_ptr[0], head_ptr[1], head_ptr[2], input_h_, input_w_, conf_thres_, iou_thres_, 1.0f,
                             1.0f, zps, scales, &group);

        rknn_outputs_release(ctx, static_cast<uint32_t>(outputs.size()), outputs.data());

        for (int i = 0; i < group.count; ++i) {
            const auto& r = group.results[i];
            if (r.id != 0) continue;  // 只取 person (COCO class 0)
            // 模型空间 → 源图空间: 去 pad 后除缩放比
            const float x0 = (static_cast<float>(r.box.left) - lb_pad_x_) / lb_ratio_;
            const float y0 = (static_cast<float>(r.box.top) - lb_pad_y_) / lb_ratio_;
            const float x1 = (static_cast<float>(r.box.right) - lb_pad_x_) / lb_ratio_;
            const float y1 = (static_cast<float>(r.box.bottom) - lb_pad_y_) / lb_ratio_;
            const int ix0 = std::clamp(static_cast<int>(x0), 0, src_w_ - 1);
            const int iy0 = std::clamp(static_cast<int>(y0), 0, src_h_ - 1);
            const int ix1 = std::clamp(static_cast<int>(x1), 0, src_w_ - 1);
            const int iy1 = std::clamp(static_cast<int>(y1), 0, src_h_ - 1);
            if (ix1 <= ix0 || iy1 <= iy0) continue;
            Detection det;
            det.bbox = cv::Rect(ix0, iy0, ix1 - ix0, iy1 - iy0);
            det.score = r.prop;
            detections.push_back(det);
        }
        return detections;
    }

  private:
    bool initInputIoMem(const rknn_tensor_attr& input_attr) {
        input_io_attr_ = input_attr;
        input_io_attr_.index = 0;
        input_io_attr_.fmt = RKNN_TENSOR_NHWC;
        input_io_attr_.type = RKNN_TENSOR_UINT8;
        input_io_attr_.pass_through = 0;
        input_io_attr_.n_elems = static_cast<uint32_t>(input_w_ * input_h_ * input_c_);
        input_io_attr_.size = input_io_attr_.n_elems;
        input_io_attr_.w_stride = static_cast<uint32_t>(input_w_);
        input_io_attr_.h_stride = static_cast<uint32_t>(input_h_);
        input_io_attr_.size_with_stride = input_io_attr_.size;

        const uint32_t size = input_io_attr_.size;
        own_fd_ = AllocDmaHeap(size);
        if (own_fd_ < 0) {
            std::cerr << "[yolo] dma_heap alloc failed\n";
            return false;
        }
        own_size_ = size;
        own_va_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, own_fd_, 0);
        if (own_va_ == MAP_FAILED) {
            std::cerr << "[yolo] dma_heap mmap failed\n";
            own_va_ = nullptr;
            close(own_fd_);
            own_fd_ = -1;
            return false;
        }
        input_mem_ = rknn_create_mem_from_fd(ctx, own_fd_, own_va_, size, 0);
        if (!input_mem_) {
            std::cerr << "[yolo] rknn_create_mem_from_fd failed\n";
            munmap(own_va_, size);
            own_va_ = nullptr;
            close(own_fd_);
            own_fd_ = -1;
            return false;
        }
        int ret = rknn_set_io_mem(ctx, input_mem_, &input_io_attr_);
        if (ret != RKNN_SUCC) {
            std::cerr << "[yolo] rknn_set_io_mem failed: " << ret << "\n";
            rknn_destroy_mem(ctx, input_mem_);
            input_mem_ = nullptr;
            return false;
        }
        std::cout << "[yolo] io_mem input (dma_heap self-alloc): size=" << input_mem_->size << " fd=" << input_mem_->fd
                  << "\n";
        return true;
    }

    bool runPreparedDma(const PreprocessedFrame& input) {
        if (!input.source_dma.valid() || !input_mem_ || input_mem_->fd < 0 || ctx == 0) {
            return false;
        }
        const int src_format = ToRgaFormat(input.source_dma.format);
        if (src_format == 0) return false;

        // letterbox 参数 (两段式: 优先用上游 meta)
        int resized_w = input_w_, resized_h = input_h_, pad_x = 0, pad_y = 0;
        if (letterbox_) {
            if (input.letterbox.valid && input.letterbox.input_width == input_w_ &&
                input.letterbox.input_height == input_h_ && input.letterbox.resized_width > 0 &&
                input.letterbox.resized_height > 0) {
                resized_w = input.letterbox.resized_width;
                resized_h = input.letterbox.resized_height;
                pad_x = input.letterbox.pad_x;
                pad_y = input.letterbox.pad_y;
            } else {
                const float scale =
                    std::min(static_cast<float>(input_w_) / static_cast<float>(input.source_dma.width),
                             static_cast<float>(input_h_) / static_cast<float>(input.source_dma.height));
                resized_w = std::max(1, static_cast<int>(std::round(input.source_dma.width * scale)));
                resized_h = std::max(1, static_cast<int>(std::round(input.source_dma.height * scale)));
                pad_x = (input_w_ - resized_w) / 2;
                pad_y = (input_h_ - resized_h) / 2;
            }
        }
        // 记录给坐标回映射用
        lb_pad_x_ = static_cast<float>(pad_x);
        lb_pad_y_ = static_cast<float>(pad_y);
        lb_ratio_ = static_cast<float>(resized_w) / static_cast<float>(input.source_dma.width);
        src_w_ = input.source_dma.width;
        src_h_ = input.source_dma.height;

        rga_buffer_t src =
            input.source_dma.fd >= 0
                ? wrapbuffer_fd_t(input.source_dma.fd, input.source_dma.width, input.source_dma.height,
                                  input.source_dma.width, input.source_dma.height, src_format)
                : wrapbuffer_virtualaddr_t(input.source_dma.ptr, input.source_dma.width, input.source_dma.height,
                                           input.source_dma.width, input.source_dma.height, src_format);
        rga_buffer_t dst = wrapbuffer_fd_t(input_mem_->fd, input_w_, input_h_, input_w_, input_h_, RK_FORMAT_RGB_888);
        im_rect full_rect{0, 0, input_w_, input_h_};
        // letterbox 几何恒定 ⇒ 灰边整帧填充只需一次 (第二帧起为纯冗余)
        const bool geom_changed = pad_x != last_fill_pad_x_ || pad_y != last_fill_pad_y_ ||
                                  resized_w != last_fill_resized_w_ || resized_h != last_fill_resized_h_;
        if (geom_changed) {
            IM_STATUS status = imfill(dst, full_rect, 0x727272, IM_SYNC);
            if (!IsRgaOk(status)) {
                std::cerr << "[yolo] RGA fill failed: " << imStrError_t(status) << "\n";
                return false;
            }
            last_fill_pad_x_ = pad_x;
            last_fill_pad_y_ = pad_y;
            last_fill_resized_w_ = resized_w;
            last_fill_resized_h_ = resized_h;
        }

        rga_buffer_t pat{};
        im_rect src_rect{0, 0, input.source_dma.width, input.source_dma.height};
        im_rect dst_rect{pad_x, pad_y, resized_w, resized_h};
        im_rect pat_rect{0, 0, 0, 0};
        // 强制走 RGA3: 本板 RGA2 无 >4G 寻址能力, src(MPP VDEC dma-buf) 可能在 4G
        // 以上。
        im_opt_t opt{};
        opt.core = RGA3_SCHEDULER_CORE0 | RGA3_SCHEDULER_CORE1;
        IM_STATUS status = improcess(src, dst, pat, src_rect, dst_rect, pat_rect, -1, nullptr, &opt, IM_SYNC);
        if (!IsRgaOk(status)) {
            std::cerr << "[yolo] RGA fd->fd letterbox failed: " << imStrError_t(status) << "\n";
            return false;
        }

        // 本仓库 librknnrt 1.5.3: 驱动在 rknn_run 时自动 flush/invalidate cache,
        // 无 rknn_mem_sync API — 无需显式同步.
        int ret = rknn_run(ctx, nullptr);
        if (ret != RKNN_SUCC) {
            std::cerr << "[yolo] rknn_run failed: " << ret << "\n";
            return false;
        }
        app::LogRknnPerfRun(ctx, "yolov5s_person");
        if (!logged_path_) {
            std::cout << "[yolo] zero-copy path: decode fd=" << input.source_dma.fd
                      << " → RGA → rknn input fd=" << input_mem_->fd << "\n";
            logged_path_ = true;
        }
        return true;
    }

    rknn_context ctx = 0;
    rknn_tensor_attr input_io_attr_{};
    std::vector<rknn_tensor_attr> output_attrs_;
    std::vector<int32_t> out_zps_;
    std::vector<float> out_scales_;
    std::vector<int> head_order_{0, 1, 2};
    rknn_tensor_mem* input_mem_ = nullptr;
    int own_fd_ = -1;
    void* own_va_ = nullptr;
    size_t own_size_ = 0;

    int input_w_ = 0, input_h_ = 0, input_c_ = 0;
    // 上次 imfill 的 letterbox 几何 (变化时才重填灰边)
    int last_fill_pad_x_ = -1, last_fill_pad_y_ = -1, last_fill_resized_w_ = -1, last_fill_resized_h_ = -1;
    float conf_thres_ = 0.5f;
    float iou_thres_ = 0.4f;
    bool letterbox_ = true;

    float lb_pad_x_ = 0.0f, lb_pad_y_ = 0.0f, lb_ratio_ = 1.0f;
    int src_w_ = 0, src_h_ = 0;
    bool logged_path_ = false;
};

}  // namespace

std::unique_ptr<Detector> CreateYolov5sDetector() { return std::make_unique<Yolov5sDetector>(); }

}  // namespace app
