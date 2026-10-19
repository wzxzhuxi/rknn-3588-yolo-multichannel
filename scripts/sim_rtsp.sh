#!/usr/bin/env bash
# sim_rtsp.sh — 推送并以【单 adb 会话】启动板上 N×RTSP 模拟源
# 用法: ./scripts/sim_rtsp.sh [N=10] [起始端口=8554] [视频=media/sample_pedestrians.h264]
#       ./scripts/sim_rtsp.sh stop
set -euo pipefail
cd "$(dirname "$0")/.."
N=${1:-10}
BASE_PORT=${2:-8554}
VIDEO=${3:-media/sample_pedestrians.h264}
BOARD_DIR=${BOARD_DIR:-/userdata/rknn-3588-yolo-multichannel}
VIDEO_NAME=$(basename "$VIDEO")
PID_FILE=/tmp/rknn3588_yolo_sim.pid

if [[ ${1:-} == "stop" ]]; then
    [[ -f $PID_FILE ]] && kill $(cat $PID_FILE) 2>/dev/null || true
    rm -f $PID_FILE
    adb shell 'killall rtsp_file_server sim_launcher.sh 2>/dev/null' || true
    echo "[+] sim stack stopped"
    exit 0
fi

adb push scripts/board/sim_launcher.sh "$BOARD_DIR/" > /dev/null
adb shell "chmod +x ${BOARD_DIR}/sim_launcher.sh; mkdir -p ${BOARD_DIR}/logs"
# 视频缺则推 (adbd 无退出码, 用输出判断)
if ! adb shell "ls ${BOARD_DIR}/${VIDEO_NAME}" 2>/dev/null | grep -q "$VIDEO_NAME"; then
    adb push "$VIDEO" "${BOARD_DIR}/" | tail -1
fi

"$0" stop 2>/dev/null || true
sleep 1
adb shell "${BOARD_DIR}/sim_launcher.sh ${N} ${BASE_PORT} ${BOARD_DIR}/${VIDEO_NAME}" \
    > /tmp/sim_launcher.log 2>&1 &
echo $! > $PID_FILE
sleep 4
echo "[+] sim stack started (single held adb session, pid $(cat $PID_FILE))"
echo "[+] video: ${VIDEO}"
echo "[+] ports: $(adb shell "ss -tln | grep -c '85[0-9][0-9]'" | tr -d '\r') listeners"
