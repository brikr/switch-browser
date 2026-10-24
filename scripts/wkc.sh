#!/usr/bin/env bash
# Runs the loader with the usual arguments filled in. Extra arguments are passed through, e.g.:
#   scripts/wkc.sh --url https://example.com/ --screenshot out.png --eval document.title
#
# Environment:
#   NXB_CORE     engine generation, 3 (default) or 0
#   NXB_ENGINE   prepare_firmware.py output dir (default firmware/22.5.0/engine)
#   NXB_DATA     writable data dir for the engine's mounts/cache (default ./nxb-data)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORE="${NXB_CORE:-3}"
ENGINE="${NXB_ENGINE:-$ROOT/firmware/22.5.0/engine}"
case "$CORE" in
  3) ENGINE_DIR="$ENGINE/engine3" ;;
  0) ENGINE_DIR="$ENGINE/engine" ;;
  *) echo "NXB_CORE must be 0 or 3" >&2; exit 2 ;;
esac

exec qemu-aarch64-static "$ROOT/loader/build/wkc-loader-core$CORE" \
  --engine "$ENGINE_DIR" --romfs "$ENGINE/romfs" --fonts "$ENGINE/fonts" --certs "$ENGINE/certs" "$@"
