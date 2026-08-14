#!/usr/bin/env bash
# Inject this project's sources/configs into the vendor SDK and apply patches.
# Idempotent: safe to re-run. Detects already-applied patches.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="${BES_SDK:-$(cd "$REPO_ROOT/.." && pwd)/bes}"
BOARD="boards/best2003_ep/aos_evb_ax4d"

die() { echo "ERROR: $*" >&2; exit 1; }
[ -f "$SDK/build.sh" ] || die "SDK build.sh not found under '$SDK' (set BES_SDK)"
[ -d "$SDK/$BOARD" ]   || die "board dir '$BOARD' not found under SDK"

copy_dir() {  # src/ dst/  (mirror, delete extras in dst)
  mkdir -p "$2"
  if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete \
      --exclude='*.o' --exclude='*.a' --exclude='*.d' \
      --exclude='.built' --exclude='.depend' --exclude='Make.dep' \
      "$1"/ "$2"/
  else
    rm -rf "$2"; mkdir -p "$2"; cp -a "$1"/. "$2"/
  fi
}

echo "== app sources =="
copy_dir "$REPO_ROOT/app/wavetable_synth" "$SDK/apps/wavetable_synth"
copy_dir "$REPO_ROOT/app/daile_btsink"    "$SDK/apps/daile_btsink"

echo "== board configs =="
for cfg in a7_wtsynth ap_wtsynth a7_dzq wavetable; do
  mkdir -p "$SDK/$BOARD/configs/$cfg"
  cp -a "$REPO_ROOT/board/configs/$cfg/." "$SDK/$BOARD/configs/$cfg/"
done

echo "== board src =="
cp -a "$REPO_ROOT/board/src/daile_i2s_mirror.c" "$SDK/$BOARD/src/"
cp -a "$REPO_ROOT/board/src/daile_i2s_mirror.h" "$SDK/$BOARD/src/"
cp -a "$REPO_ROOT/board/common/daile_i2s_mirror.h" "$SDK/boards/common/src/"

echo "== sdk tools =="
mkdir -p "$SDK/tools"
cp -a "$REPO_ROOT/scripts/sdk-tools/." "$SDK/tools/"
chmod +x "$SDK"/tools/*.sh 2>/dev/null || true

apply_patch() {  # git_dir patch_file
  local dir="$1" patch="$2"
  [ -s "$patch" ] || { echo "  skip empty $(basename "$patch")"; return 0; }
  if git -C "$dir" apply --reverse --check "$patch" >/dev/null 2>&1; then
    echo "  already applied: $(basename "$patch")"
  elif git -C "$dir" apply --check "$patch" >/dev/null 2>&1; then
    git -C "$dir" apply "$patch"
    echo "  applied: $(basename "$patch")"
  else
    echo "  WARN: clean apply failed for $(basename "$patch"), trying 3-way..."
    if git -C "$dir" apply --3way "$patch"; then
      echo "  applied via 3-way: $(basename "$patch")"
    else
      echo "  ERROR: manual merge needed for $(basename "$patch")" >&2
      return 1
    fi
  fi
}

echo "== patches =="
apply_patch "$SDK"                    "$REPO_ROOT/patches/sdk-root.patch"
apply_patch "$SDK/framework/services" "$REPO_ROOT/patches/framework-services.patch"

echo "OK: SDK prepared at $SDK"
