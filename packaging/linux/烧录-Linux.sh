#!/usr/bin/env bash
# 用法：先看串口号  ls /dev/ttyUSB*
# 然后：./烧录-Linux.sh /dev/ttyUSB0

set -e
PORT="${1:-/dev/ttyUSB0}"
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

echo "即将用 $PORT 全量烧录 BES2600"
chmod +x ./dldtool

./dldtool "$PORT" programmer2003.bin \
  -M nuttx_bl.bin \
  --addr 0x2C040000 nuttx_ota.bin \
  --addr 0x2C0C0000 nuttx_ota.bin \
  --addr 0x2C850000 nuttx_a7.bin \
  -M nuttx_ap.bin \
  --pgm-rate 2000000

echo "烧录结束"
