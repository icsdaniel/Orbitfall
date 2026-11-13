#!/usr/bin/env bash
# Builds the wasm GDExtension (no threads) and exports the "Web" preset.
#
#   tools/export_web.sh [--out DIR] [--debug] [--main-scene res://path.tscn]
#                       [--version LABEL] [--kind KIND] [--pr N] [--skip-extension]
#
# Needs emcc on PATH (Emscripten 4.0.10, the version Godot 4.5 was built with):
#   source /opt/tools/emsdk/emsdk_env.sh
# --main-scene exports the same project with another start scene (used for toys);
# project.godot is restored afterwards.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT="build/web"; TARGET="template_release"; EXPORT_FLAG="--export-release"
MAIN_SCENE=""; VERSION=""; KIND="local"; PR="0"; SKIP_EXT="0"
while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    --debug) TARGET="template_debug"; EXPORT_FLAG="--export-debug"; shift ;;
    --main-scene) MAIN_SCENE="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --kind) KIND="$2"; shift 2 ;;
    --pr) PR="$2"; shift 2 ;;
    --skip-extension) SKIP_EXT="1"; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

if [ "$SKIP_EXT" != "1" ]; then
  command -v emcc >/dev/null || { echo "emcc not on PATH; source emsdk_env.sh first" >&2; exit 1; }
  scons -C gdextension platform=web threads=no target="$TARGET" -j"$(nproc)"
fi
WASM="bin/liborbitfall.web.${TARGET}.wasm32.nothreads.wasm"
[ -f "$WASM" ] || { echo "missing $WASM" >&2; exit 1; }

python3 tools/write_build_info.py --version "$VERSION" --kind "$KIND" --pr "$PR" --target "$TARGET"

restore() { [ -f project.godot.orig ] && mv -f project.godot.orig project.godot; return 0; }
trap restore EXIT
if [ -n "$MAIN_SCENE" ]; then
  [ -f "${MAIN_SCENE#res://}" ] || { echo "main scene not found: $MAIN_SCENE" >&2; exit 1; }
  cp project.godot project.godot.orig
  sed -i "s|^run/main_scene=.*|run/main_scene=\"${MAIN_SCENE}\"|" project.godot
fi

rm -rf "$OUT"; mkdir -p "$OUT"
# Import can exit non-zero on a first run while still producing a valid cache.
godot --headless --path . --import >/dev/null 2>&1 || true
godot --headless --path . "$EXPORT_FLAG" "Web" "$OUT/index.html"

for f in index.html index.js index.wasm index.pck; do
  [ -s "$OUT/$f" ] || { echo "export failed: $OUT/$f missing" >&2; exit 1; }
done
ls "$OUT"/*.side.wasm >/dev/null 2>&1 || ls "$OUT"/liborbitfall*.wasm >/dev/null 2>&1 \
  || { echo "export failed: extension wasm not in $OUT" >&2; exit 1; }

cp build_info.json "$OUT/build_info.json"
python3 tools/inject_version_tag.py "$OUT/index.html" build_info.json
echo "web export ready: $OUT ($(du -sh "$OUT" | cut -f1))"
