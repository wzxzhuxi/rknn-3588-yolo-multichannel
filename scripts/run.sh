#!/usr/bin/env bash
# run.sh — 板上运行 rknn3588_yolo_app (锁频 + 启动). 用法: ./scripts/run.sh [config/xxx.conf] [额外 env]
set -euo pipefail
cd "$(dirname "$0")/.."
BOARD_DIR=${BOARD_DIR:-/userdata/rknn-3588-yolo-multichannel}
CONF=${1:-config/yolov5_only.conf}

# 锁 CPU/NPU/DDR 到最高频率 (与 yolorun.sh 一致; DDR 默认 528MHz 会拖性能)
adb shell '
for i in 0 1 2 3 4 5 6 7; do
    echo 1 > /sys/devices/system/cpu/cpu$i/cpuidle/state1/disable 2>/dev/null
done
for spec in "policy0 1800000" "policy4 2304000" "policy6 2304000"; do
    p=$(echo $spec | cut -d" " -f1); f=$(echo $spec | cut -d" " -f2)
    echo userspace > /sys/devices/system/cpu/cpufreq/$p/scaling_governor
    echo $f        > /sys/devices/system/cpu/cpufreq/$p/scaling_setspeed
done
echo userspace  > /sys/class/devfreq/fdab0000.npu/governor
echo 1000000000 > /sys/class/devfreq/fdab0000.npu/userspace/set_freq
echo userspace  > /sys/class/devfreq/dmc/governor
echo 1848000000 > /sys/class/devfreq/dmc/userspace/set_freq
' > /dev/null
echo "[+] freqs locked"

# 崩溃取证: core dump 落到持久分区 (一次性, 板上 root 直接生效)
adb shell 'ulimit -c unlimited; echo /userdata/rknn-3588-yolo-multichannel/core.%p > /proc/sys/kernel/core_pattern 2>/dev/null; echo "[+] core_pattern set"'

# 多路时 fd 需求随通道数增长，默认 1024 会先于程序能力触顶.
adb shell "cd ${BOARD_DIR} && ulimit -Hn 65535 2>/dev/null; ulimit -Sn 65535 2>/dev/null; export LD_LIBRARY_PATH=lib && exec ./rknn3588_yolo_app ${CONF}"
