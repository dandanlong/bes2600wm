#!/usr/bin/env bash
# Serial debug wrapper -> runs the SDK-side helper (needs prepare-sdk.sh first).
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="${BES_SDK:-$(cd "$REPO_ROOT/.." && pwd)/bes}"
TOOL="$SDK/tools/serial_debug.sh"
[ -x "$TOOL" ] || { echo "ERROR: $TOOL missing; run ./scripts/prepare-sdk.sh first" >&2; exit 1; }
exec "$TOOL" "$@"
