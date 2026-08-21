#!/usr/bin/env bash
# Collect built firmware images from the SDK into out/firmware/<timestamp>/.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="${BES_SDK:-$(cd "$REPO_ROOT/.." && pwd)/bes}"
NUTTX="$SDK/rtos/nuttx"

STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$REPO_ROOT/out/firmware/$STAMP"
mkdir -p "$OUT"

copied=0
for b in nuttx_bl nuttx_ota nuttx_a7 nuttx_ap nuttx_apc1; do
  if [ -f "$NUTTX/$b.bin" ]; then
    cp -a "$NUTTX/$b.bin" "$OUT/"
    copied=$((copied+1))
  fi
done

# include the flasher program if present
for p in "$SDK/prebuild/m0/programmer2003.bin" "$SDK/prebuild/m0/dldtool"; do
  [ -f "$p" ] && cp -a "$p" "$OUT/"
done

[ "$copied" -gt 0 ] || { echo "ERROR: no nuttx_*.bin found in $NUTTX (build first?)" >&2; exit 1; }

ln -sfn "$STAMP" "$REPO_ROOT/out/firmware/latest"
echo "OK: $copied image(s) exported to $OUT"
ls -lh "$OUT"
