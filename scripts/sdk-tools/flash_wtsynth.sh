#!/usr/bin/env bash
# Flash full SDK AX4D layout for ap_wtsynth + a7_wtsynth sound demo.
#
# Layout (same as boards/.../aos_evb_ax4d/Readme):
#   nuttx_bl.bin  (bootloader, -M)
#   nuttx_ota.bin @ 0x2C040000 and 0x2C0C0000
#   nuttx_a7.bin  @ 0x2C850000
#   nuttx_ap.bin  (-M)
#
# Usage:
#   ./tools/flash_wtsynth.sh
#   ./tools/flash_wtsynth.sh --hw-reset both
#   ./tools/flash_wtsynth.sh --no-reboot   # press RESET yourself
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DZQ="$ROOT/dzq"
BINS="$DZQ/bins"
BAUD="${FLASH_BAUD:-921600}"
NUTTX="$ROOT/rtos/nuttx"
DLDTOOL="$ROOT/prebuild/m0/dldtool"
PGM="$ROOT/prebuild/m0/programmer2003.bin"
LOG="${FLASH_LOG:-/tmp/flash_wtsynth.log}"
REBOOT_MODE="${FLASH_REBOOT_MODE:-hw-both}"
RESET_MS="${FLASH_RESET_MS:-100}"
RETRY="${FLASH_RETRY:-20}"

BL="${VENDOR_BL:-$BINS/nuttx_bl.bin}"
OTA="${VENDOR_OTA:-$BINS/nuttx_ota.bin}"
APC1="${VENDOR_APC1:-$BINS/nuttx_apc1.bin}"
# Prefer dzq/bins vendor images (self-built bl/ota can break A7 bringup).
[[ -e "$BL" ]] || BL="$NUTTX/nuttx_bl.bin"
[[ -e "$OTA" ]] || OTA="$NUTTX/nuttx_ota.bin"
[[ -e "$APC1" ]] || APC1="$NUTTX/nuttx_apc1.bin"
A7="$NUTTX/nuttx_a7.bin"
AP="$NUTTX/nuttx_ap.bin"

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
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 1 ;;
  esac
done

for f in "$DLDTOOL" "$PGM" "$BL" "$OTA" "$A7" "$AP"; do
  [[ -e "$f" ]] || { echo "missing $f" >&2; exit 1; }
done
if [[ ! -e "$APC1" ]]; then
  echo "[flash_wtsynth] WARN: missing $APC1 (dzq layout expects apc1 @ 0x28B00000)" >&2
fi
[[ -e "$PORT" ]] || { echo "missing port $PORT" >&2; exit 1; }
[[ -x "$DLDTOOL" ]] || { echo "not executable: $DLDTOOL" >&2; exit 1; }

# Flash the A7 package as built (LZMA bin1/bin2, same as dzq vendor layout).
# AP subsys_load_aarch32_bl() decompresses the BL; A7 BL handles bin1/bin2.
FLASH_A7="$A7"
if [[ "${FLASH_WTSYNTH_EXPAND_A7:-0}" == "1" ]]; then
  FLASH_A7="/tmp/nuttx_a7.wtsynth.flash.bin"
  cp -f "$A7" "$FLASH_A7"
  python3 - "$FLASH_A7" <<'PY'
from pathlib import Path
import lzma, shutil, struct, sys

def align4(n):
    return (n + 3) // 4 * 4

def pad_extra(n):
    return 4 if (n % 4) == 0 else 0

pkg_path = Path(sys.argv[1])
orig = Path('/tmp/nuttx_a7.wtsynth.orig.bin')
if orig.exists() and orig.stat().st_size < pkg_path.stat().st_size:
    shutil.copyfile(orig, pkg_path)
    print(f'[flash_wtsynth] restored compressed A7 from {orig}', flush=True)

pkg = bytearray(pkg_path.read_bytes())
MAGIC = 0xBE57EC1C
assert struct.unpack_from('<I', pkg, 0)[0] == MAGIC, 'bad A7 magic'
bl_size = struct.unpack_from('<I', pkg, 0x10)[0]
off = 0x20
bin1 = off + align4(bl_size) + pad_extra(bl_size)
bin1_size = struct.unpack_from('<I', pkg, bin1)[0]
bin1_data = bin1 + 4
bin2 = bin1_data + align4(bin1_size) + pad_extra(bin1_size)
bin2_size = struct.unpack_from('<I', pkg, bin2)[0]
bin2_data = bin2 + 4
print(f'[flash_wtsynth] A7 pkg in: bl={bl_size} bin1={bin1_size} bin2={bin2_size}', flush=True)

def maybe_lzma(buf):
    return lzma.decompress(buf) if buf[:1] == b'\x5d' else bytes(buf)

bin1_raw = maybe_lzma(bytes(pkg[bin1_data:bin1_data + bin1_size]))
bin2_raw = maybe_lzma(bytes(pkg[bin2_data:bin2_data + bin2_size]))
if bin1_size < 30000 and bin2_size < 700000 and pkg[bin1_data:bin1_data+1] == b'\x5d':
    shutil.copyfile(pkg_path, orig)
    print(f'[flash_wtsynth] saved compressed A7 {orig}', flush=True)

new = bytearray()
new += bytes(pkg[:bin1])
new += struct.pack('<I', len(bin1_raw))
new += bin1_raw
new += bytes(align4(len(bin1_raw)) - len(bin1_raw))
new += bytes(pad_extra(len(bin1_raw)))
new += struct.pack('<I', len(bin2_raw))
new += bin2_raw
pkg_path.write_bytes(new)
print(f'[flash_wtsynth] A7 pkg out: bin1 raw {len(bin1_raw)} bin2 raw {len(bin2_raw)} total {len(new)}', flush=True)
PY
fi

if [[ "$FLASH_A7" == "$A7" ]]; then
  echo "[flash_wtsynth] A7 flash as-built $(stat -c%s "$A7") bytes (set FLASH_WTSYNTH_EXPAND_A7=1 to expand bin1/bin2)"
fi

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
print(f"[flash_wtsynth] armed soft reboot on {port}", flush=True)
PY
}

hw_reset_pulse() {
  local which="$1"
  python3 - "$PORT" "$BAUD" "$which" "$RESET_MS" <<'PY'
import serial, sys, time
port, baud, which, ms = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4])
ser = serial.Serial(port=port, baudrate=baud, timeout=0.2, write_timeout=2,
                    dsrdtr=False, rtscts=False)
ser.dtr = True
ser.rts = True
time.sleep(0.02)
if which in ("dtr", "both"):
    ser.dtr = False
if which in ("rts", "both"):
    ser.rts = False
print(f"[flash_wtsynth] HW {which.upper()} HOLD reset {ms}ms", flush=True)
time.sleep(ms / 1000.0)
ser.dtr = True
ser.rts = True
print(f"[flash_wtsynth] HW {which.upper()} RELEASE — dldtool next", flush=True)
ser.close()
PY
}

run_dld() {
  set +e
  local apc1_arg=""
  if [[ -e "$APC1" ]]; then
    apc1_arg="-M \"$APC1\""
  fi
  script -q -c "\"$DLDTOOL\" -v --force-uart -b \"$BAUD\" --reboot --retry \"$RETRY\" \"$PORT\" \"$PGM\" \
    -M \"$BL\" \
    --addr 0x2C040000 \"$OTA\" \
    --addr 0x2C0C0000 \"$OTA\" \
    --addr 0x2C850000 \"$FLASH_A7\" \
    -M \"$AP\" \
    $apc1_arg" "$LOG"
  rc=$?
  set -e
  return "$rc"
}

echo "[flash_wtsynth] bl=$BL"
echo "[flash_wtsynth] ota=$OTA @ 0x2C040000 / 0x2C0C0000"
echo "[flash_wtsynth] a7=$FLASH_A7 @ 0x2C850000"
echo "[flash_wtsynth] ap=$AP"
echo "[flash_wtsynth] apc1=$APC1"
echo "[flash_wtsynth] reboot=$REBOOT_MODE port=$PORT"
: > "$LOG"

case "$REBOOT_MODE" in
  soft) arm_soft_reboot ;;
  none) echo "[flash_wtsynth] waiting for SYNC — press RESET" ;;
  hw-dtr) hw_reset_pulse dtr ;;
  hw-rts) hw_reset_pulse rts ;;
  hw-both) hw_reset_pulse both ;;
  *) echo "bad REBOOT_MODE=$REBOOT_MODE" >&2; exit 1 ;;
esac

run_dld || true
tail -n 40 "$LOG" | sed 's/\r$//'
if grep -q "PROGRAMMING SUCCEEDED" "$LOG"; then
  echo "[flash_wtsynth] OK"
  exit 0
fi
echo "[flash_wtsynth] FAILED — check wiring / images" >&2
exit 1
