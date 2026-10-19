#!/usr/bin/env bash
# build.sh — RK3588 交叉构建. 用法: ./scripts/build.sh [-jN]
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR=${BUILD_DIR:-build-cross}
cmake -S . -B ${BUILD_DIR} \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_aarch64.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build ${BUILD_DIR} ${1:--j$(nproc)}
echo "[+] artifacts:"
ls -la ${BUILD_DIR}/hello_gst ${BUILD_DIR}/rknn3588_yolo_app 2>/dev/null || true
