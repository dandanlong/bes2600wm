#!/usr/bin/env bash
# Build the wavetable synth firmware via the vendor SDK build.sh.
# Usage:
#   ./scripts/build.sh                 # default config set
#   ./scripts/build.sh a7_wtsynth      # build only one config
#   ./scripts/build.sh cfg distclean   # pass-through extra args to SDK build.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="${BES_SDK:-$(cd "$REPO_ROOT/.." && pwd)/bes}"
BOARD="boards/best2003_ep/aos_evb_ax4d"

[ -f "$SDK/build.sh" ] || { echo "ERROR: SDK build.sh not found under '$SDK'" >&2; exit 1; }

DEFAULT_CONFIGS=(bootloader ota a7_wtsynth ap_wtsynth)

cd "$SDK"
if [ $# -ge 1 ] && [ "${1:0:1}" != "-" ]; then
  # single config (+ optional extra args like distclean/-j)
  cfg="$1"; shift
  echo ">> build $cfg $*"
  ./build.sh "$BOARD/configs/$cfg" "${@:--j}"
else
  for cfg in "${DEFAULT_CONFIGS[@]}"; do
    echo ">> build $cfg"
    ./build.sh "$BOARD/configs/$cfg" -j
  done
fi

echo "OK: firmware at $SDK/rtos/nuttx/nuttx_*.bin"
echo "    run ./scripts/export-firmware.sh to collect into out/"
