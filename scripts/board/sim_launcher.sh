#!/usr/bin/env bash
# sim_launcher.sh — 板上单进程启动器: N 个 rtsp_file_server
# 由 PC 侧【单个】 adb exec 会话持有, 把长驻 adb 会话从 N+2 压到 1.
# (本板 adbd 会话容量有限, 多会话长驻已两次把 adbd 耗死)
# 用法 (板上): ./sim_launcher.sh <N> <base_port> <video> [run_dir]
set -u
N=${1:-10}
BASE=${2:-8554}
RUN=${4:-/userdata/rknn-3588-yolo-multichannel}
VIDEO=${3:-${RUN}/sample_pedestrians.h264}
SERVER_DIR=/userdata/rknn_yolov8_multi_rtsp_demo

export LD_LIBRARY_PATH=${SERVER_DIR}/lib:${RUN}/lib

i=0
while [ $i -lt $N ]; do
    port=$((BASE + i))
    ${SERVER_DIR}/rtsp_file_server "$VIDEO" "$port" "live/${i}" 30 \
        > ${RUN}/logs/sim_${i}.log 2>&1 &
    i=$((i + 1))
done
echo "[sim_launcher] $N servers up (ports $BASE..$((BASE + N - 1)))"

# 驻留: 等任意子进程退出后全杀重退 (或直接 wait 到会话被杀)
trap 'kill $(jobs -p) 2>/dev/null' EXIT
wait
