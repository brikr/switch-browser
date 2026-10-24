#!/usr/bin/env bash
# Runs the loader with the usual arguments filled in. Extra arguments are passed through, e.g.:
#   scripts/wkc.sh --url https://example.com/ --screenshot out.png --eval document.title
#   scripts/wkc.sh --serve 8000 [--url https://example.com/]   # then open http://127.0.0.1:8000/
#
# The loader is a static Linux AArch64 binary. On an AArch64 Linux host it runs directly; on other
# Linux hosts it runs under qemu-aarch64 user-mode emulation. macOS (including Apple silicon) can't
# run Linux binaries, so use a Linux VM or container there (an arm64 one runs at native speed).
#
# Environment:
#   NXB_CORE     engine generation, 3 (default) or 0
#   NXB_FIRMWARE firmware version directory prepared by prepare_firmware.py (default firmware/22.5.0)
#   NXB_DATA     writable data dir for the engine's mounts/cache (default ./nxb-data)
#   NXB_RUNNER   command to run the loader with, overriding the choice above; set it empty to run
#                directly (e.g. with binfmt_misc), or e.g. "qemu-aarch64-static -g 1234" to debug
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORE="${NXB_CORE:-3}"
FIRMWARE="${NXB_FIRMWARE:-$ROOT/firmware/22.5.0}"
ENGINE="$FIRMWARE/engine"
case "$CORE" in
  3) ENGINE_DIR="$ENGINE/engine3" ;;
  0) ENGINE_DIR="$ENGINE/engine" ;;
  *) echo "NXB_CORE must be 0 or 3" >&2; exit 2 ;;
esac

LOADER="$ROOT/loader/build/wkc-loader-core$CORE"
[ -x "$LOADER" ] || { echo "$LOADER not built; run: make -C loader CORE=$CORE" >&2; exit 1; }
[ -d "$ENGINE_DIR" ] || { echo "$ENGINE_DIR missing; run: python3 -I scripts/prepare_firmware.py $FIRMWARE" >&2; exit 1; }

if [ -n "${NXB_RUNNER+set}" ]; then
  read -r -a RUNNER <<< "$NXB_RUNNER"
elif [ "$(uname -s)" != Linux ]; then
  echo "The loader is a Linux AArch64 binary; on $(uname -s), run it in a Linux VM or container." >&2
  exit 1
elif [ "$(uname -m)" = aarch64 ]; then
  RUNNER=()
elif command -v qemu-aarch64-static >/dev/null; then
  RUNNER=(qemu-aarch64-static)
elif command -v qemu-aarch64 >/dev/null; then
  RUNNER=(qemu-aarch64)
else
  echo "This $(uname -m) host needs qemu-aarch64 (user-mode) to run the loader, e.g. the qemu-user-static package." >&2
  exit 1
fi

exec ${RUNNER[@]+"${RUNNER[@]}"} "$LOADER" \
  --engine "$ENGINE_DIR" --romfs "$ENGINE/romfs" --fonts "$ENGINE/fonts" --certs "$ENGINE/certs" \
  --ui-dir "$ROOT/frontend" "$@"
