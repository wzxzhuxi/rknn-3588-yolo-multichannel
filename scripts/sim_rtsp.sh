#!/usr/bin/env bash
# sim_rtsp.sh — 推送并以【单 adb 会话】启动板上 N×RTSP 模拟源
# 用法: ./scripts/sim_rtsp.sh [N=10] [起始端口=8554] | ./scripts/sim_rtsp.sh stop
set -euo pipefail
cd "$(dirname "$0")/.."
N=${1:-10}
BASE_PORT=${2:-8554}
PID_FILE=/tmp/rknn3588_yolo_sim.pid

if [[ ${1:-} == "stop" ]]; then
    [[ -f $PID_FILE ]] && kill $(cat $PID_FILE) 2>/dev/null || true
    rm -f $PID_FILE
    adb shell 'killall rtsp_file_server sim_launcher.sh 2>/dev/null' || true
    echo "[+] sim stack stopped"
    exit 0
fi

adb push scripts/board/sim_launcher.sh /userdata/rknn-3588-yolo-multichannel/ > /dev/null
adb shell 'chmod +x /userdata/rknn-3588-yolo-multichannel/sim_launcher.sh; mkdir -p /userdata/rknn-3588-yolo-multichannel/logs'
# 视频缺则推 (adbd 无退出码, 用输出判断)
if ! adb shell 'ls /userdata/rknn-3588-yolo-multichannel/720p60hz.h264' 2>/dev/null | grep -q h264; then
    adb push /home/fuck/rk3588/rk3588_linux6.12_sdk/app/rknn-3588-npu-yolo-accelerate-saturated/720p60hz.h264 \
        /userdata/rknn-3588-yolo-multichannel/ | tail -1
fi

"$0" stop 2>/dev/null || true
sleep 1
adb shell "/userdata/rknn-3588-yolo-multichannel/sim_launcher.sh ${N} ${BASE_PORT} /userdata/rknn-3588-yolo-multichannel/720p60hz.h264" \
    > /tmp/sim_launcher.log 2>&1 &
echo $! > $PID_FILE
sleep 4
echo "[+] sim stack started (single held adb session, pid $(cat $PID_FILE))"
echo "[+] ports: $(adb shell "ss -tln | grep -c '85[0-9][0-9]'" | tr -d '\r') listeners"
