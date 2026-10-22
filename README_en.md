# rknn-3588-yolo-multichannel

[中文](README.md) | **English**

Multi-channel RTSP person detection on RK3588 (ATK-DLRK3588, 16 GB, kernel 6.12.69). The pipeline uses a single model, YOLOv5s (COCO class 0 = person), with dma-buf zero-copy from RTSP capture to NPU inference.

## Features

- Concurrent multi-channel RTSP with configurable channel count (validation limit: 128)
- GStreamer + MPP hardware decode, RGA NV12 → RGB letterbox, RKNN consumes RGA output directly
- YOLOv5s person detection with boxes in source-image coordinates
- IoU-only tracking (no embeddings)
- Optional local JPEG snapshots (CPU JPEG, throttled per track)
- Per-channel capture/engine threads + shared NPU worker pool; frames are dropped under backpressure, never pushed back to RTSP
- No MQTT, face models, or Python runtime required

## Project statement

A multi-channel RTSP YOLOv5s inference project on RK3588, supporting arbitrary numbers of concurrent video streams (current config validation limit: 128). Each channel captures and decodes independently while sharing an NPU worker pool. When inference capacity is exceeded, frames are dropped by design instead of applying backpressure to the video source. The inference rate achievable per channel decreases as concurrency grows.

## Layout

```
components/
  common_config/   config parsing, channel/env overrides
  pre_infer/       RTSP capture, MPP decode, dma-buf, RGA preprocessing
  infer/           YOLOv5s RKNN zero-copy detector
  post_infer/      IoU tracker, snapshots
src/               main application and board smoke tools
scripts/           build, deploy, run, simulate, forensics
config/            multi-channel sample configs
models/            YOLOv5s RKNN model
media/             sample pedestrian test video
3rdparty/          vendored OpenCV / RGA / glibc23 shim
librknn_api/       RKNN runtime and headers
```

## Model

The model is included in the repository:

```
models/yolov5s_relu_pt.rknn
```

- 640×640 input, uint8, NHWC
- Only COCO class 0 (person) is kept
- Keep the filename when replacing it, or update `yolo_model` in the config

## Build

Requirements:

- aarch64 buildroot cross toolchain (default in `cmake/toolchain_aarch64.cmake`, override with `TOOLCHAIN_ROOT`)
- vendored OpenCV, RGA and RKNN runtime in this repository
- GStreamer / glib in the toolchain sysroot

```bash
./scripts/build.sh          # output to build-cross/
./scripts/build.sh -j4      # custom parallelism
```

Artifacts:

- `build-cross/rknn3588_yolo_app` — main application
- `build-cross/config_dump` / `detect_dump` / `rtsp_source_dump` / `hello_gst` — board diagnostic tools

## Board (adb)

```bash
./scripts/deploy.sh                         # push app, librknnrt, model, configs
./scripts/run.sh config/yolov5_only.conf    # lock frequencies + run
./scripts/stop.sh                           # stop the app
./scripts/stop.sh --all                     # also stop simulated streams
```

Board directory: `/userdata/rknn-3588-yolo-multichannel/`.

Simulated RTSP:

```bash
./scripts/sim_rtsp.sh 10 8554   # start 10 simulated streams
./scripts/sim_rtsp.sh stop
```

Sample video:

- `media/sample_pedestrians.h264` — pedestrian scene for functional smoke/demo
- `sim_rtsp.sh` uses it by default; pass a third argument for another local H.264 file:
  `./scripts/sim_rtsp.sh 10 8554 path/to/video.h264`

Crash forensics:

```bash
./scripts/board_forensics.sh [outdir]
```

## Configuration

Sample configs:

| File | Purpose |
|---|---|
| `config/yolov5_only.conf` | default, 10 channels, snapshots/BGR disabled |
| `config/yolov5_only_16x25.conf` | 16 channels × 25 fps |
| `config/yolov5_only_12x30.conf` | 12 channels × 30 fps throughput test |
| `config/yolov5_only_36.conf` | 36-channel throughput test |

Key options:

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

- `channels.list` only reorders; it does **not** filter. Remove `channel.camN.stream_url` to remove a channel
- `yolo_core_mask=rotate` binds worker `w` to NPU core `w % 3`; `auto` lets the driver schedule
- Local snapshots: `keep_source_bgr=true` + `enable_snapshot=true`; files go to `snapshot_dir` (default `snapshots/`)
- Environment variables override config, e.g. `CHANNELS_LIST`, `TARGET_FPS`, `CONF_THRES`, `YOLO_CORE_MASK`

## Concurrency performance

Test setup: RK3588, 720p H.264, `target_fps=10`, `npu_workers=12`, `yolo_core_mask=rotate`, about 30 s per point. `Inference fps` means frames actually completed through YOLOv5, not frames read from RTSP.

| Channels | Read fps/ch | Total read fps | Inference fps | Inference fps/ch | NPU avg | App RSS | System memory |
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

\* The 1-channel NPU average includes startup warm-up; steady state is about 16 ms.  
\** Channels 1–90 use one RTSP server per channel, so system memory includes simulator overhead. The 100-channel point uses one server with 100 clients to avoid OOM from 100 simulator processes, which is why 90 channels reports higher system memory than 100.

Additional: the 16 × 25 fps config measured ~400 read fps (25 fps per channel), ~147 inference fps (~9.2 fps per channel), and ~273 MB app RSS. The ~300 read-fps plateau is a source/pipeline-side limit; pure 720p MPP decode is about 1870 fps.

## Board notes

- adbd does not forward remote exit codes (always 0); do not use `test -f` to check remote files
- Do not put `libstdc++.so.6` in the board `lib/`; GStreamer C++ plugins may load it accidentally and crash
- `run.sh` raises fd soft/hard limits to 65535
- Capture evidence before reproducing a crash: `./scripts/board_forensics.sh`
- Crash backtraces are written to `/userdata/rknn-3588-yolo-multichannel/{segv,terminate,abort}.log`
