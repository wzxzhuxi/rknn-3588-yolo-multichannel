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

## Performance reference

Test setup: 720p H.264, `target_fps=10`, `npu_workers=12`, `yolo_core_mask=rotate`.

- 10 channels: ~100 read fps, ~100 inference fps, every channel can process 10 fps
- 30 channels: ~300 read fps, ~136 inference fps, ~4.5 inference fps per channel
- 100 channels (one RTSP server / 100 clients): ~304 read fps, ~135 inference fps, app RSS ~5.6 GB, stable
- 16 channels × 25 fps: ~400 read fps (25 fps per channel achieved), ~147 inference fps (~9.2 fps per channel), app RSS ~273 MB
- YOLOv5s inference takes ~75–82 ms per frame; inference throughput is NPU-bound. The ~300 read-fps plateau comes from the source/pipeline side, not from MPP decode (pure 720p decode is far faster)

The performance table uses 720p H.264; the bundled sample video is a 1080p pedestrian scene and is intended for functional smoke tests, not as a performance baseline.

For multi-channel load tests, use **one RTSP server with multiple clients**. Each `rtsp_file_server` process uses ~122 MB RSS; 100 processes cause OOM and do not represent the application limit.

## Board notes

- adbd does not forward remote exit codes (always 0); do not use `test -f` to check remote files
- Do not put `libstdc++.so.6` in the board `lib/`; GStreamer C++ plugins may load it accidentally and crash
- `run.sh` raises fd soft/hard limits to 65535
- Capture evidence before reproducing a crash: `./scripts/board_forensics.sh`
- Crash backtraces are written to `/userdata/rknn-3588-yolo-multichannel/{segv,terminate,abort}.log`
