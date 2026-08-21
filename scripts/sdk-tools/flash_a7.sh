#!/usr/bin/env bash
# Flash A7 for MiAiSoundbox product AP + SDK ax4d wavetable A7.
#
# Product AP loads from 0x2C500000:
#   [uint32 BE size][first lzma = A7 BL]
# A7 bl (CONFIG_A7_BIN_OFFSET=0x850000, SMP 2 CPUs) then loads bin1/bin2 from:
#   0x2C850000 packaged nuttx_a7.bin (magic 0xbe57ec1c)
#
# Default / required flash path (强制烧录):
#   ./tools/flash_a7.sh
#   ./tools/flash_a7.sh --hw-reset both
#
# FT232 DTR#/RTS# pulse board RESET (active-HIGH):
#   pyserial False => pin HIGH => HOLD reset
#   pyserial True  => pin LOW  => RELEASE
# Sequence: HOLD -> RELEASE -> dldtool Wait for SYNC immediately.
#
# Other modes (debug only):
#   --hw-reset dtr|rts
#   --soft-reboot
#   --no-reboot   # press RESET yourself
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${FLASH_PORT:-/dev/ttyUSB0}"
BAUD="${FLASH_BAUD:-921600}"
ADDR_AP="${FLASH_A7_AP_ADDR:-0x2C500000}"
ADDR_PKG="${FLASH_A7_PKG_ADDR:-0x2C850000}"
DLDTOOL="$ROOT/prebuild/m0/dldtool"
PGM="$ROOT/prebuild/m0/programmer2003.bin"
BIN="$ROOT/rtos/nuttx/nuttx_a7.bin"
LOG="${FLASH_LOG:-/tmp/flash_a7.log}"
# 强制默认：FT232 DTR+RTS 硬件复位
REBOOT_MODE="${FLASH_REBOOT_MODE:-hw-both}"
RESET_MS="${FLASH_RESET_MS:-100}"
RETRY="${FLASH_RETRY:-20}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --hw-reset)
      case "${2:-}" in
        dtr|DTR) REBOOT_MODE=hw-dtr ;;
        rts|RTS) REBOOT_MODE=hw-rts ;;
        both|BOTH) REBOOT_MODE=hw-both ;;
        *) echo "usage: --hw-reset dtr|rts|both" >&2; exit 1 ;;
      esac
      shift 2
      ;;
    --soft-reboot) REBOOT_MODE=soft; shift ;;
    --no-reboot|--no-soft-reboot) REBOOT_MODE=none; shift ;;
    --reset-ms) RESET_MS="$2"; shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
    *) BIN="$1"; shift ;;
  esac
done

[[ -x "$DLDTOOL" && -f "$PGM" && -f "$BIN" && -e "$PORT" ]] || {
  echo "missing tool/image/port ($PORT)" >&2; exit 1;
}

python3 - "$BIN" <<'PY'
"""Build product + expand bin1/bin2 to raw; always flash BOTH addrs."""
from pathlib import Path
import lzma
import shutil
import struct
import sys

def align4(n):
    return (n + 3) // 4 * 4

def pad_extra(n):
    return 4 if (n % 4) == 0 else 0

pkg_path = Path(sys.argv[1])
# Prefer clean compressed build; fall back to BIN if no orig saved this run.
orig = Path('/tmp/nuttx_a7.orig.bin')
if orig.exists() and orig.stat().st_size < pkg_path.stat().st_size:
    # current BIN already expanded; restore compressed source
    shutil.copyfile(orig, pkg_path)
    print(f'[flash_a7] restored compressed pkg from {orig} ({orig.stat().st_size})')

pkg = bytearray(pkg_path.read_bytes())
MAGIC = 0xBE57EC1C

full_path = Path('/tmp/a7_full.bin.tmp')
# Always refresh full.bin.tmp from current compressed pkg (avoid stale BL)
idx = bytes(pkg).find(b'\x5d\x00\x00\x00')
if idx < 0:
    raise SystemExit('no lzma in package')
full = bytes(pkg[32:] if struct.unpack_from('<I', pkg, 0)[0] == MAGIC else pkg[idx:])
full_path.write_bytes(full)

assert struct.unpack_from('<I', pkg, 0)[0] == MAGIC
bl_size = struct.unpack_from('<I', pkg, 0x10)[0]
off = 0x20
bin1 = off + align4(bl_size) + pad_extra(bl_size)
bin1_size = struct.unpack_from('<I', pkg, bin1)[0]
bin1_data = bin1 + 4
bin2 = bin1_data + align4(bin1_size) + pad_extra(bin1_size)
bin2_size = struct.unpack_from('<I', pkg, bin2)[0]
bin2_data = bin2 + 4
print(f'[flash_a7] pkg in: bl={bl_size} bin1={bin1_size} bin2={bin2_size}')

# Detect already-raw payloads (first byte != lzma props 0x5d)
def maybe_lzma(buf):
    if buf[:1] == b'\x5d':
        return lzma.decompress(buf)
    return bytes(buf)

bin1_raw = maybe_lzma(bytes(pkg[bin1_data:bin1_data + bin1_size]))
bin2_raw = maybe_lzma(bytes(pkg[bin2_data:bin2_data + bin2_size]))
# Save compressed snapshot for next flash before we overwrite BIN
if bin1_size < 20000 and bin2_size < 200000:
    shutil.copyfile(pkg_path, orig)
    print(f'[flash_a7] saved compressed orig {orig} ({pkg_path.stat().st_size})')

new = bytearray()
new += bytes(pkg[:bin1])
new += struct.pack('<I', len(bin1_raw))
new += bin1_raw
new += bytes(align4(len(bin1_raw)) - len(bin1_raw))
new += bytes(pad_extra(len(bin1_raw)))
new += struct.pack('<I', len(bin2_raw))
new += bin2_raw
pkg_path.write_bytes(new)
print(f'[flash_a7] pkg out: bin1 raw {len(bin1_raw)} bin2 raw {len(bin2_raw)} total {len(new)}')

full = full_path.read_bytes()
dec = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE)
out = dec.decompress(full)
assert dec.eof, 'first lzma stream incomplete'
first = full[: len(full) - len(dec.unused_data)]
assert len(out) > 0 and len(first) == bl_size, (len(out), len(first), bl_size)
prod = len(first).to_bytes(4, 'big') + first
Path('/tmp/a7_product.bin').write_bytes(prod)
print(f'[flash_a7] product {len(prod)} bytes @0x2C500000 + pkg {len(new)} @0x2C850000')
PY

if command -v fuser >/dev/null 2>&1; then
  fuser -k "$PORT" 2>/dev/null || true
  sleep 0.2
fi

arm_soft_reboot() {
  python3 - "$PORT" "$BAUD" <<'PY'
import serial, sys, time
port, baud = sys.argv[1], int(sys.argv[2])
ser = serial.Serial(port, baud, timeout=0.2, write_timeout=2)
ser.write(b"~."); ser.flush(); time.sleep(0.15)
ser.write(b"\r"); ser.flush(); time.sleep(0.2)
ser.reset_input_buffer()
for cmd in (b"sleep 1\r", b"sleep 1\r", b"reboot\r"):
    ser.write(cmd); ser.flush(); time.sleep(0.05)
ser.close()
print(f"[flash_a7] armed sleep1;sleep1;reboot on {port}", flush=True)
PY
}

# Active-HIGH RESET via FT232: HOLD (pin HIGH) then RELEASE (pin LOW), then dldtool.
hw_reset_pulse() {
  local which="$1"  # dtr|rts|both
  python3 - "$PORT" "$BAUD" "$which" "$RESET_MS" <<'PY'
import serial, sys, time
port, baud, which, ms = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4])
ser = serial.Serial(port=port, baudrate=baud, timeout=0.2, write_timeout=2,
                    dsrdtr=False, rtscts=False)
# released baseline (LOW)
ser.dtr = True
ser.rts = True
time.sleep(0.02)
# HOLD reset: active-HIGH => pyserial False => pin HIGH
if which in ("dtr", "both"):
    ser.dtr = False
if which in ("rts", "both"):
    ser.rts = False
print(f"[flash_a7] HW {which.upper()} HOLD reset (pin HIGH) {ms}ms", flush=True)
time.sleep(ms / 1000.0)
# RELEASE
ser.dtr = True
ser.rts = True
print(f"[flash_a7] HW {which.upper()} RELEASE (pin LOW) — dldtool next", flush=True)
ser.close()
PY
}

run_dld() {
  set +e
  script -q -c "\"$DLDTOOL\" -v --force-uart -b \"$BAUD\" --reboot --retry \"$RETRY\" \"$PORT\" \"$PGM\" --addr \"$ADDR_AP\" /tmp/a7_product.bin --addr \"$ADDR_PKG\" \"$BIN\"" "$LOG"
  rc=$?
  set -e
  return "$rc"
}

echo "[flash_a7] $ADDR_AP <= /tmp/a7_product.bin  (AP load, BE+lzma)"
echo "[flash_a7] $ADDR_PKG <= $BIN  (A7 bl secondary, magic pkg)"
echo "[flash_a7] reboot=$REBOOT_MODE port=$PORT retry=$RETRY"
: > "$LOG"

case "$REBOOT_MODE" in
  soft)
    arm_soft_reboot
    ;;
  none)
    echo "[flash_a7] waiting for SYNC — press RESET"
    ;;
  hw-dtr)
    hw_reset_pulse dtr
    ;;
  hw-rts)
    hw_reset_pulse rts
    ;;
  hw-both)
    hw_reset_pulse both
    ;;
  *)
    echo "bad REBOOT_MODE=$REBOOT_MODE" >&2
    exit 1
    ;;
esac

run_dld || true
tail -n 40 "$LOG" | sed 's/\r$//'
if grep -q "PROGRAMMING SUCCEEDED" "$LOG"; then
  echo "[flash_a7] OK (mode=$REBOOT_MODE, dual A7 slots)"
  exit 0
fi
echo "[flash_a7] FAILED — check FT232 DTR/RTS → RESET wiring" >&2
exit 1
