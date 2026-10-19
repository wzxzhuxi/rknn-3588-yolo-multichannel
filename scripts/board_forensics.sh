#!/usr/bin/env bash
# board_forensics.sh — 崩溃/异常后抓取证包到 PC (先跑这个再复现!)
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-./forensics-$(date +%Y%m%d_%H%M%S)}
mkdir -p "$OUT"

adb pull /userdata/rknn-3588-yolo-multichannel/segv.log "$OUT/" 2>/dev/null || true
adb pull /userdata/rknn-3588-yolo-multichannel/terminate.log "$OUT/" 2>/dev/null || true
adb pull /userdata/rknn-3588-yolo-multichannel/abort.log "$OUT/" 2>/dev/null || true
adb shell 'dmesg' > "$OUT/dmesg.txt" 2>/dev/null || true
adb shell 'ls -la /userdata/rknn-3588-yolo-multichannel/ /userdata/rknn-3588-yolo-multichannel/logs/ 2>/dev/null' > "$OUT/board_files.txt" || true
adb pull /userdata/rknn-3588-yolo-multichannel/logs "$OUT/logs" 2>/dev/null || true
# core dump (如果开了 core_pattern)
adb shell 'ls /userdata/rknn-3588-yolo-multichannel/core.* 2>/dev/null' | tr -d '\r' | while read f; do
    adb pull "$f" "$OUT/" 2>/dev/null || true
done
echo "[+] forensics in $OUT:"
ls -la "$OUT"
