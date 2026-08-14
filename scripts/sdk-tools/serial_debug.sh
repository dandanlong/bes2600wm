#!/usr/bin/env bash
# Serial debug helper for ap_wtsynth / a7_wtsynth on AX4D EVB.
#
# Usage:
#   ./tools/serial_debug.sh boot          # reboot + capture boot log
#   ./tools/serial_debug.sh probe         # dump A7 probe / preload tags
#   ./tools/serial_debug.sh shell         # interactive serial (picocom)
#   ./tools/serial_debug.sh a7            # rptun start + try ttyAUDIO
#   ./tools/serial_debug.sh flash         # flash ap+a7 images
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${SERIAL_PORT:-/dev/ttyUSB0}"
BAUD="${SERIAL_BAUD:-921600}"

usage() { sed -n '2,12p' "$0"; exit "${1:-0}"; }

[[ $# -ge 1 ]] || usage 1
[[ -e "$PORT" ]] || { echo "missing $PORT (plug FTDI USB-UART)" >&2; exit 1; }

if command -v fuser >/dev/null 2>&1; then
  fuser -k "$PORT" 2>/dev/null || true
  sleep 0.2
fi

run_py() {
  local mode="$1"
  python3 - "$PORT" "$BAUD" "$mode" <<'PY'
import serial, sys, time, re
port, baud, mode = sys.argv[1], int(sys.argv[2]), sys.argv[3]

def strip(s):
    return re.sub(r'\x1b\[[0-9;?]*[A-Za-z]', '', s)

def cmd(ser, c, wait=1.5):
    ser.reset_input_buffer()
    ser.write((c + '\r').encode())
    ser.flush()
    time.sleep(wait)
    return strip(ser.read(65536).decode('utf-8', 'replace'))

ser = serial.Serial(port, baud, timeout=0.3)

if mode == 'boot':
    ser.write(b'reboot\r')
    ser.flush()
    print(f'[serial_debug] reboot on {port}, capturing 30s...')
    t0 = time.time()
    while time.time() - t0 < 30:
        chunk = ser.read(4096).decode('utf-8', 'replace')
        if not chunk:
            continue
        for ln in chunk.splitlines():
            if any(k in ln for k in [
                'apwt>', 'probe', 'preload', '__a7_dsp', 'a7 boot entry',
                'ap_wtsynth', 'ttyAUDIO', 'wtsynth', 'a7wt>', 'FAIL', 'panic'
            ]):
                print(ln)
elif mode == 'probe':
    ser.write(b'\r')
    time.sleep(0.5)
    for _ in range(30):
        ser.write(b'\r')
        ser.flush()
        time.sleep(0.3)
        if 'apwt>' in ser.read(4096).decode('utf-8', 'replace'):
            break
    else:
        print('[serial_debug] warn: apwt> prompt not seen', file=sys.stderr)
    addrs = [
        ('probe[0]', '0x38021000', 4),
        ('probe[31] tag', '0x3802107c', 4),
        ('BL entry', '0x38000020', 16),
        ('bin2@PSRAM', '0x38100000', 4),
        ('bin1', '0x38056000', 4),
    ]
    for label, addr, n in addrs:
        out = cmd(ser, f'xd {addr} {n}', 1.8)
        print(f'=== {label} ({addr}) ===')
        for ln in out.splitlines():
            if 'Hex dump' in ln or re.search(r'^[0-9a-f]{4}:', ln):
                print(ln)
elif mode == 'a7':
    for c in [
        'rptun start /dev/rptun/audio',
        'sleep 3',
        'ls /dev/tty*',
        'xd 0x38021000 4',
    ]:
        out = cmd(ser, c, 2.0 if 'sleep' in c else 1.5)
        print(f'>>> {c}')
        for ln in out.splitlines():
            if 'Hex dump' in ln or re.search(r'^[0-9a-f]{4}:', ln) or 'tty' in ln or 'probe' in ln:
                print(ln)
    print('[serial_debug] try: cu -l /dev/ttyAUDIO  (from apwt> on live board)')
else:
    print(f'unknown py mode {mode}', file=sys.stderr)
    sys.exit(1)

ser.close()
PY
}

case "$1" in
  boot)
    run_py boot
    ;;
  probe)
    run_py probe
    ;;
  a7)
    run_py a7
    ;;
  shell)
    if command -v picocom >/dev/null 2>&1; then
      exec picocom -b "$BAUD" "$PORT"
    elif command -v minicom >/dev/null 2>&1; then
      exec minicom -D "$PORT" -b "$BAUD"
    else
      echo "install picocom or minicom; or: cu -l $PORT -s $BAUD" >&2
      exit 1
    fi
    ;;
  flash)
    exec "$ROOT/tools/flash_wtsynth.sh" --hw-reset both "${@:2}"
    ;;
  -h|--help)
    usage 0
    ;;
  *)
    echo "unknown command: $1" >&2
    usage 1
    ;;
esac
