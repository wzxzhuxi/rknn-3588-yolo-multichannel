#!/usr/bin/env bash
# stop.sh — 停止板上的 rknn3588_yolo_app (+ 可选停模拟源)
set -euo pipefail
adb shell 'killall -TERM rknn3588_yolo_app 2>/dev/null; sleep 2; killall -KILL rknn3588_yolo_app 2>/dev/null; echo stopped'
if [[ ${1:-} == "--all" ]]; then
    "$(dirname "$0")/sim_rtsp.sh" stop
fi
