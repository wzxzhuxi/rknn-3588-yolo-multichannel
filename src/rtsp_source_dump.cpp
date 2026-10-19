// RTSP DMABuf 源板上冒烟: 拉 N 帧, 打印 fd/stride/尺寸/dmabuf 统计
#include <chrono>
#include <iostream>
#include <string>

#include "calc_pre_infer/video_source.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <rtsp_url> [frames=30] [codec=h264]\n";
        return 1;
    }
    const std::string url = argv[1];
    const int want = argc > 2 ? std::atoi(argv[2]) : 30;

    calc_pre::PreInferConfig cfg;
    cfg.stream_url = url;
    if (argc > 3) cfg.codec = argv[3];

    auto src = calc_pre::CreateVideoSource(cfg);
    std::string err;
    if (!src || !src->open(cfg, &err)) {
        std::cerr << "[dump] open failed: " << err << "\n";
        return 2;
    }

    const auto t0 = std::chrono::steady_clock::now();
    int got = 0, fd_ok = 0;
    for (int i = 0; i < want; ++i) {
        calc_pre::RawFrame frame;
        if (!src->read(&frame)) {
            std::cerr << "[dump] read failed at frame " << i << "\n";
            break;
        }
        ++got;
        if (frame.dma.fd > 0) ++fd_ok;
        if (i < 3 || i == want - 1) {
            std::cout << "[dump] frame=" << frame.meta.frame_index
                      << " fd=" << frame.dma.fd
                      << " ptr=" << frame.dma.ptr
                      << " size=" << frame.dma.width << "x" << frame.dma.height
                      << " stride=" << frame.dma.width_stride << "/" << frame.dma.height_stride
                      << " buf_size=" << frame.dma.size
                      << " pts_us=" << frame.meta.pts_us << "\n";
        }
        src->release_frame(frame.dma.buffer_index);
    }
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    auto info = src->info();
    std::cout << "[dump] source=" << info.description
              << " got=" << got << "/" << want
              << " fd_frames=" << fd_ok
              << " elapsed=" << sec << "s"
              << " fps=" << (sec > 0 ? got / sec : 0.0) << "\n";
    src->close();

    if (got == 0) return 3;
    // 全 dmabuf = 0; 有 fallback = 4
    return fd_ok == got ? 0 : 4;
}
