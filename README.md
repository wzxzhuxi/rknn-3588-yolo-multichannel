# rknn-3588-yolo-multichannel

**中文** | [English](README_en.md)

RK3588（ATK-DLRK3588，16GB，kernel 6.12.69）上的多路 RTSP 检测项目。流水线只使用一个模型：YOLOv5s（COCO class 0 = person），从 RTSP 拉流到 NPU 推理全程走 dma-buf 零拷贝。

## 功能

- 多路 RTSP 并发，通道数可配置（配置校验上限 128）
- GStreamer + MPP 硬解，RGA 完成 NV12 → RGB letterbox，RKNN 直接消费 RGA 输出
- YOLOv5s person 检测，输出源图像素坐标
- 纯 IoU 跟踪（无 embedding）
- 可选本地抓拍（CPU JPEG，按 track 节流）
- 每通道独立采集/后处理线程 + 共享 NPU worker 池，队列满时丢帧，不反压 RTSP
- 无需 MQTT、人脸模型或 Python 运行时

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

## 性能参考

测试条件：720p H.264、`target_fps=10`、`npu_workers=12`、`yolo_core_mask=rotate`。

- 10 路：读流约 100 fps，推理约 100 fps，每路 10 fps 可全帧处理
- 30 路：读流约 300 fps，推理约 136 fps，平均每路约 4.5 fps 完成推理
- 100 路（单 RTSP server/100 客户端）：读流约 304 fps，推理约 135 fps，App RSS 约 5.6 GB，稳定运行
- 16 路 × 25 fps：读流约 400 fps（每路 25 fps 达标），推理约 147 fps（平均每路约 9.2 fps），App RSS 约 273 MB
- YOLOv5s 单次 NPU 推理约 75–82 ms，推理吞吐受 NPU 限制；读流约 300 fps 是供流/管线侧瓶颈，纯 720p MPP 解码远高于此

性能表使用 720p H.264；仓库示例视频为 1080p 行人场景，仅用于功能冒烟，不作为性能基线。

多路压测建议使用**单 RTSP server + 多客户端**。每个 `rtsp_file_server` 约 122 MB RSS，100 个进程会导致 OOM，不能代表应用上限。

## 板端注意事项

- 本板 adbd 不转发远程退出码（恒为 0），不要用 `test -f` 判断远程文件
- 不要往板端 `lib/` 放 `libstdc++.so.6`，否则 GStreamer C++ 插件会误载崩溃
- `run.sh` 会把 fd soft/hard limit 提升到 65535
- 异常先取证再复现：`./scripts/board_forensics.sh`
- 主程序崩溃回溯写入 `/userdata/rknn-3588-yolo-multichannel/{segv,terminate,abort}.log`
