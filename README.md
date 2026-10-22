# rknn-3588-yolo-multichannel

**中文** | [English](README_en.md)

## 项目说明

基于 RK3588（ATK-DLRK3588，16GB，kernel 6.12.69）的多路 RTSP 视频流 YOLOv5s 推理项目，支持任意并发数量视频流推理（当前配置校验上限 128 路）。从 RTSP 拉流到 NPU 推理全程走 dma-buf 零拷贝,每路独立拉流/解码，共享 NPU worker 池；推理能力不足时按设计丢帧，不反压视频源。实际每路可完成的推理帧率会随并发数增加而下降。

## 功能

- 多路 RTSP 并发，通道数可配置（配置校验上限 128）
- GStreamer + MPP 硬解，RGA 完成 NV12 → RGB letterbox，RKNN 直接消费 RGA 输出
- YOLOv5s person 检测，输出源图像素坐标
- 纯 IoU 跟踪（无 embedding）
- 可选本地抓拍（CPU JPEG，按 track 节流）
- 每通道独立采集/后处理线程 + 共享 NPU worker 池，队列满时丢帧，不反压 RTSP
- 无需 MQTT、人脸模型或 Python 运行时

## 并发性能

测试条件：RK3588，720p H.264，`target_fps=10`，`npu_workers=12`，`yolo_core_mask=rotate`，每档运行约 30 s。`推理完成` 是实际完成 YOLOv5 推理的帧率，不是读流帧率。

| 并发路数 | 每路读 fps | 总读 fps | 推理完成 fps | 每路推理 fps | NPU avg | App RSS | 系统内存 |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 10.2 | 10.2 | 10.2 | 10.2 | 28.6 ms* | 252 MB | 5.9% |
| 10 | 9.7 | 97.4 | 99.8 | 10.0 | 55.0 ms | 264 MB | 14.3% |
| 20 | 9.8 | 196.4 | 144.2 | 7.2 | 76.8 ms | 277 MB | 23.6% |
| 30 | 9.8 | 294.8 | 136.0 | 4.5 | 81.1 ms | 292 MB | 32.8% |
| 40 | 7.8 | 313.2 | 134.4 | 3.4 | 82.3 ms | 399 MB | 44.0% |
| 50 | 6.1 | 305.6 | 134.2 | 2.7 | 82.4 ms | 499 MB | 53.9% |
| 60 | 5.1 | 308.6 | 133.6 | 2.2 | 82.5 ms | 606 MB | 63.6% |
| 70 | 4.4 | 308.0 | 133.2 | 1.9 | 82.8 ms | 785 MB | 74.1% |
| 80 | 3.8 | 300.2 | 133.2 | 1.7 | 83.0 ms | 914 MB | 84.3% |
| 90 | 3.3 | 299.6 | 132.8 | 1.5 | 83.1 ms | 1019 MB | 94.3% |
| 100** | 3.1 | 311.6 | 134.4 | 1.3 | 82.0 ms | 5.6 GB | 58.1% |

\* 1 路 NPU avg 含启动预热，稳定后约 16 ms。  
\** 1–90 路每路使用独立 RTSP server，系统内存包含模拟器开销；100 路使用 1 个 server/100 客户端，避免 100 个模拟进程 OOM，因此 90 路的系统内存反而高于 100 路。

补充：16 路 × 25 fps 配置实测读流约 400 fps（每路 25 fps 达标），推理约 147 fps（每路约 9.2 fps），App RSS 约 273 MB。读流约 300 fps 是供流/管线侧瓶颈，纯 720p MPP 解码约 1870 fps。

## 目录结构

```
components/
  common_config/   配置解析、通道/环境变量覆盖
  pre_infer/       RTSP 拉流、MPP 解码、dma-buf、RGA 预处理
  infer/           YOLOv5s RKNN 零拷贝检测
  post_infer/      IoU 跟踪、抓拍
src/               主程序与板端冒烟工具
scripts/           构建、部署、运行、模拟流、取证
config/            多路配置样例
models/            YOLOv5s RKNN 模型
media/             示例行人测试视频
3rdparty/          vendored OpenCV / RGA / glibc23 shim
librknn_api/       RKNN runtime 与头文件
```

## 模型

仓库已包含模型：

```
models/yolov5s_relu_pt.rknn
```

- 输入 640×640，uint8，NHWC
- 只保留 COCO class 0（person）
- 替换模型时保持文件名一致，或同步修改配置里的 `yolo_model`

## 构建

依赖：

- aarch64 buildroot 交叉工具链（默认路径见 `cmake/toolchain_aarch64.cmake`，可用 `TOOLCHAIN_ROOT` 覆盖）
- 仓库内 vendored OpenCV、RGA、RKNN runtime
- sysroot 内的 GStreamer / glib

```bash
./scripts/build.sh          # 输出到 build-cross/
./scripts/build.sh -j4      # 指定并行度
```

产物：

- `build-cross/rknn3588_yolo_app` — 主程序
- `build-cross/config_dump` / `detect_dump` / `rtsp_source_dump` / `hello_gst` — 板端诊断工具

## 上板（adb）

```bash
./scripts/deploy.sh                         # 推送程序、librknnrt、模型、配置
./scripts/run.sh config/yolov5_only.conf    # 锁频 + 运行
./scripts/stop.sh                           # 停止应用
./scripts/stop.sh --all                     # 同时停止模拟流
```

板端目录：`/userdata/rknn-3588-yolo-multichannel/`。

模拟 RTSP：

```bash
./scripts/sim_rtsp.sh 10 8554   # 启动 10 路模拟流
./scripts/sim_rtsp.sh stop
```

示例视频：

- `media/sample_pedestrians.h264` — 行人场景，用于功能冒烟/演示
- `sim_rtsp.sh` 默认使用该视频；第三个参数可指定其它本地 H.264 文件：
  `./scripts/sim_rtsp.sh 10 8554 path/to/video.h264`

崩溃取证：

```bash
./scripts/board_forensics.sh [outdir]
```

## 配置

常用配置：

| 文件 | 用途 |
|---|---|
| `config/yolov5_only.conf` | 默认 10 路，快照/BGR 关闭 |
| `config/yolov5_only_16x25.conf` | 16 路 × 25 fps |
| `config/yolov5_only_12x30.conf` | 12 路 × 30 fps 吞吐压测 |
| `config/yolov5_only_36.conf` | 36 路吞吐压测 |

关键项：

```ini
input_mode=rtsp
yolo_model=models/yolov5s_relu_pt.rknn
conf_thres=0.35
iou_thres=0.45
target_fps=25
npu_workers=12
yolo_core_mask=rotate

channels.list=cam0,cam1
channel.cam0.stream_url=rtsp://127.0.0.1:8554/live/0
channel.cam1.stream_url=rtsp://127.0.0.1:8555/live/1
```

- `channels.list` 只重排序，**不过滤**；减少通道要删除对应的 `channel.camN.stream_url`
- `yolo_core_mask=rotate`：worker 按 `w % 3` 绑定 NPU 核心；`auto` 由驱动调度
- 本地抓拍：`keep_source_bgr=true` + `enable_snapshot=true`，图片写入 `snapshot_dir`（默认 `snapshots/`）
- 环境变量可覆盖配置，例如 `CHANNELS_LIST`、`TARGET_FPS`、`CONF_THRES`、`YOLO_CORE_MASK`

## 板端注意事项

- 本板 adbd 不转发远程退出码（恒为 0），不要用 `test -f` 判断远程文件
- 不要往板端 `lib/` 放 `libstdc++.so.6`，否则 GStreamer C++ 插件会误载崩溃
- `run.sh` 会把 fd soft/hard limit 提升到 65535
- 异常先取证再复现：`./scripts/board_forensics.sh`
- 主程序崩溃回溯写入 `/userdata/rknn-3588-yolo-multichannel/{segv,terminate,abort}.log`
