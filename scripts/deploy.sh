#!/usr/bin/env bash
# deploy.sh — 推送构建产物到 3588 板 (/userdata/rknn-3588-yolo-multichannel/). 用法: ./scripts/deploy.sh
# 注意: 本板 adbd 不转发远程命令退出码 (恒为 0), 不能用 test -f 做存在性判断 — 一律直推.
set -euo pipefail
cd "$(dirname "$0")/.."
BOARD_DIR=${BOARD_DIR:-/userdata/rknn-3588-yolo-multichannel}
TCROOT=${TOOLCHAIN_ROOT:-/home/fuck/rk3588/rk3588_linux6.12_sdk/buildroot/output/alientek_rk3588/host}

adb shell "mkdir -p ${BOARD_DIR}/lib ${BOARD_DIR}/models ${BOARD_DIR}/config ${BOARD_DIR}/logs"

# 2026-10-14 起目标板为 6.12 系统 (NPU 驱动 0.9.8): rga/mpp 用系统自带版本,
# 只推 rootfs 没有的 librknnrt(2.3.2).
# 注意: 不要往 lib/ 放 libstdc++.so.6 — exe 已静态链接 C++ 运行时; 板端 gst
# C++ 插件 (libgstmpp.so) 会经 LD_LIBRARY_PATH 误载工具链 6.0.33 而崩.
[[ -f librknn_api/aarch64/librknnrt.so ]] && adb push librknn_api/aarch64/librknnrt.so ${BOARD_DIR}/lib/ | tail -1

for f in build-cross/hello_gst build-cross/config_dump build-cross/rknn3588_yolo_app; do
    [[ -f $f ]] && adb push "$f" ${BOARD_DIR}/ | tail -1
done
# 模型不入 git (models/*.rknn ignored): yolo-only 链路只依赖 yolov5s_relu_pt
[[ -f models/yolov5s_relu_pt.rknn ]] && adb push models/yolov5s_relu_pt.rknn ${BOARD_DIR}/models/ | tail -1
# 全量测试配置 (yolov5_only / 36 路 / 12x30)
for c in config/*.conf; do adb push "$c" ${BOARD_DIR}/config/ | tail -1; done
echo "[+] deployed to ${BOARD_DIR}"
