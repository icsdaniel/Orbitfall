#!/usr/bin/env bash
# Cloud / CI environment setup for Orbitfall. Idempotent.
# Installs: pinned Godot editor + export templates, SCons. Optional: EMSDK=1 (web), ANDROID=1.
set -euo pipefail
GODOT_VERSION="${GODOT_VERSION:-4.5.1}"
EMSDK_VERSION="${EMSDK_VERSION:-4.0.11}"
TOOLS="${TOOLS_DIR:-/opt/tools}"
mkdir -p "$TOOLS"
python3 -m pip install -q --upgrade scons
if ! command -v godot >/dev/null || ! godot --version | grep -q "^${GODOT_VERSION}"; then
  curl -sSL -o "$TOOLS/godot.zip" "https://github.com/godotengine/godot/releases/download/${GODOT_VERSION}-stable/Godot_v${GODOT_VERSION}-stable_linux.x86_64.zip"
  unzip -q -o "$TOOLS/godot.zip" -d "$TOOLS" && rm "$TOOLS/godot.zip"
  ln -sf "$TOOLS/Godot_v${GODOT_VERSION}-stable_linux.x86_64" /usr/local/bin/godot
fi
TPL="$HOME/.local/share/godot/export_templates/${GODOT_VERSION}.stable"
if [ ! -f "$TPL/version.txt" ]; then
  mkdir -p "$TPL"
  curl -sSL -o "$TOOLS/tpl.tpz" "https://github.com/godotengine/godot/releases/download/${GODOT_VERSION}-stable/Godot_v${GODOT_VERSION}-stable_export_templates.tpz"
  unzip -q -o "$TOOLS/tpl.tpz" -d "$TOOLS/tpl" && mv "$TOOLS"/tpl/templates/* "$TPL/" && rm -rf "$TOOLS/tpl" "$TOOLS/tpl.tpz"
fi
if [ "${EMSDK:-0}" = "1" ] && [ ! -d "$TOOLS/emsdk" ]; then
  git clone -q --depth 1 https://github.com/emscripten-core/emsdk.git "$TOOLS/emsdk"
  "$TOOLS/emsdk/emsdk" install "$EMSDK_VERSION" && "$TOOLS/emsdk/emsdk" activate "$EMSDK_VERSION"
  echo "source $TOOLS/emsdk/emsdk_env.sh before 'scons platform=web'"
fi
git submodule update --init --depth 1 gdextension/godot-cpp
echo "Orbitfall toolchain ready: $(godot --version)"
