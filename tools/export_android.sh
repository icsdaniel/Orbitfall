#!/usr/bin/env bash
# Builds the arm64 GDExtension and exports a debug APK with the "Android" preset.
#
#   tools/export_android.sh [--out FILE] [--version LABEL] [--kind KIND]
#
# Needs: ANDROID_HOME (SDK with platform-tools, build-tools, NDK 28.1.13356709),
# JAVA_HOME (JDK 17). A throwaway debug keystore is generated with keytool when
# GODOT_ANDROID_KEYSTORE_DEBUG_PATH is not set. Debug builds only: never ship these.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT="build/android/orbitfall-debug.apk"; VERSION=""; KIND="local"
while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --kind) KIND="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

: "${ANDROID_HOME:?set ANDROID_HOME to the Android SDK}"
JAVA_HOME="${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")}"
GODOT_MM="$(godot --version | cut -d. -f1,2)"

scons -C gdextension platform=android arch=arm64 target=template_debug -j"$(nproc)"

if [ -z "${GODOT_ANDROID_KEYSTORE_DEBUG_PATH:-}" ]; then
  export GODOT_ANDROID_KEYSTORE_DEBUG_PATH="$ROOT/build/debug.keystore"
  export GODOT_ANDROID_KEYSTORE_DEBUG_USER="androiddebugkey"
  export GODOT_ANDROID_KEYSTORE_DEBUG_PASSWORD="android"
  if [ ! -f "$GODOT_ANDROID_KEYSTORE_DEBUG_PATH" ]; then
    mkdir -p "$(dirname "$GODOT_ANDROID_KEYSTORE_DEBUG_PATH")"
    keytool -genkeypair -keyalg RSA -keysize 2048 -validity 9999 \
      -alias "$GODOT_ANDROID_KEYSTORE_DEBUG_USER" -keystore "$GODOT_ANDROID_KEYSTORE_DEBUG_PATH" \
      -storepass "$GODOT_ANDROID_KEYSTORE_DEBUG_PASSWORD" -keypass "$GODOT_ANDROID_KEYSTORE_DEBUG_PASSWORD" \
      -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
  fi
fi

# Godot reads the SDK and JDK locations from editor settings only.
SETTINGS_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/godot"
SETTINGS="$SETTINGS_DIR/editor_settings-${GODOT_MM}.tres"
mkdir -p "$SETTINGS_DIR"
if [ ! -f "$SETTINGS" ]; then
  printf '[gd_resource type="EditorSettings" format=3]\n\n[resource]\n' > "$SETTINGS"
fi
sed -i '/^export\/android\/\(android_sdk_path\|java_sdk_path\) = /d' "$SETTINGS"
sed -i "s|^\[resource\]\$|[resource]\nexport/android/android_sdk_path = \"${ANDROID_HOME}\"\nexport/android/java_sdk_path = \"${JAVA_HOME}\"|" "$SETTINGS"

python3 tools/write_build_info.py --version "$VERSION" --kind "$KIND" --target template_debug
mkdir -p "$(dirname "$OUT")"; rm -f "$OUT"
godot --headless --path . --import >/dev/null 2>&1 || true
godot --headless --path . --export-debug "Android" "$OUT"
[ -s "$OUT" ] || { echo "export failed: $OUT missing" >&2; exit 1; }
unzip -l "$OUT" | grep -q "lib/arm64-v8a/liborbitfall" \
  || { echo "export failed: extension library not inside the APK" >&2; exit 1; }
echo "APK ready: $OUT ($(du -h "$OUT" | cut -f1))"
