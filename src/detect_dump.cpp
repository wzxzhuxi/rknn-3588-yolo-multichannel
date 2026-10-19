// 零拷贝检测链路板上冒烟: RTSP → MPP 解码 fd → RGA letterbox → NPU → 框.
// 验证点: 出框、每帧 detect 耗时、RSS 平稳 (无 CPU 帧拷贝/泄漏).
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

#include <unistd.h>

#include "app/detector.hpp"
#include "calc_pre_infer/video_source.hpp"

namespace {

double ProcRssMB() {
    std::ifstream f("/proc/self/statm");
    long size_pages = 0, resident_pages = 0;
    f >> size_pages >> resident_pages;
    return resident_pages * (::sysconf(_SC_PAGESIZE) / 1024) / 1024.0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <rtsp_url> <model.rknn> [frames=60]\n";
        return 1;
    }
    const std::string url = argv[1];
    const std::string model = argv[2];
    const int want = argc > 3 ? std::atoi(argv[3]) : 60;

    calc_pre::PreInferConfig pre_cfg;
    pre_cfg.stream_url = url;
    auto src = calc_pre::CreateVideoSource(pre_cfg);
    std::string err;
    if (!src || !src->open(pre_cfg, &err)) {
        std::cerr << "[dump] source open failed: " << err << "\n";
        return 2;
    }

    auto detector = app::CreateYolov5sDetector();
    app::DetectorOptions det_opts;
    det_opts.conf_thres = 0.45f;
    det_opts.iou_thres = 0.45f;
    det_opts.letterbox = true;
    if (!detector->init(model, det_opts)) {
        std::cerr << "[dump] detector init failed\n";
        return 3;
    }

    std::cout << "[dump] rss_start=" << ProcRssMB() << "MB\n";
    int got = 0;
    double det_ms_total = 0.0;
    for (int i = 0; i < want; ++i) {
        calc_pre::RawFrame frame;
        if (!src->read(&frame)) {
            std::cerr << "[dump] read failed at frame " << i << "\n";
            break;
        }
        ++got;

        app::PreprocessedFrame det_input;
        det_input.source_dma.fd = frame.dma.fd;
        det_input.source_dma.ptr = frame.dma.ptr;
        det_input.source_dma.width = frame.dma.width;
        det_input.source_dma.height = frame.dma.height;
        det_input.source_dma.width_stride = frame.dma.width_stride;
        det_input.source_dma.height_stride = frame.dma.height_stride;
        det_input.source_dma.format = app::DmaPixelFormat::Nv12;
        det_input.source_dma.size = frame.dma.size;

        const auto t0 = std::chrono::steady_clock::now();
        auto detections = detector->detect_preprocessed(det_input);
        const double det_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        det_ms_total += det_ms;

        if (i < 3 || (i + 1) % 30 == 0) {
            std::cout << "[dump] frame=" << frame.meta.frame_index << " fd=" << frame.dma.fd
                      << " boxes=" << detections.size() << " det_ms=" << det_ms;
            for (size_t k = 0; k < detections.size() && k < 3; ++k) {
                std::cout << " [" << detections[k].bbox.x << "," << detections[k].bbox.y << " "
                          << detections[k].bbox.width << "x" << detections[k].bbox.height << " " << detections[k].score
                          << "]";
            }
            std::cout << "\n";
        }
        src->release_frame(frame.dma.buffer_index);
    }
    std::cout << "[dump] frames=" << got << " avg_det_ms=" << (got > 0 ? det_ms_total / got : 0.0)
              << " rss_end=" << ProcRssMB() << "MB\n";
    src->close();
    return got > 0 ? 0 : 4;
}
