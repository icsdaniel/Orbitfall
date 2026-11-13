#!/usr/bin/env bash
# Cloud / CI environment setup for Orbitfall. Idempotent.
# Installs: pinned Godot editor + export templates, SCons.
# Options (environment variables):
#   EMSDK=1            Emscripten for the web build (version Godot 4.5 was built with)
#   ANDROID=1          Android SDK packages + NDK into ANDROID_HOME (needs a JDK 17)
#   TEMPLATES="web android linux"   only keep these export templates (default: all)
#   TOOLS_DIR=/opt/tools   BIN_DIR=/usr/local/bin   SKIP_SUBMODULE=1   SKIP_SCONS=1
set -euo pipefail
GODOT_VERSION="${GODOT_VERSION:-4.5.1}"
EMSDK_VERSION="${EMSDK_VERSION:-4.0.10}"          # godotengine/build-containers 4.5: Dockerfile.web
ANDROID_NDK_VERSION="${ANDROID_NDK_VERSION:-28.1.13356709}"   # godot-cpp default
ANDROID_BUILD_TOOLS="${ANDROID_BUILD_TOOLS:-35.0.0}"
ANDROID_PLATFORM="${ANDROID_PLATFORM:-android-35}"
ANDROID_CMDLINE_TOOLS="${ANDROID_CMDLINE_TOOLS:-11076708}"
TOOLS="${TOOLS_DIR:-/opt/tools}"
BIN_DIR="${BIN_DIR:-/usr/local/bin}"
TEMPLATES="${TEMPLATES:-all}"
mkdir -p "$TOOLS"

if [ "${SKIP_SCONS:-0}" != "1" ]; then
  python3 -m pip install -q --upgrade scons
fi

GODOT_BIN="$TOOLS/Godot_v${GODOT_VERSION}-stable_linux.x86_64"
if [ ! -x "$GODOT_BIN" ]; then
  curl -fsSL -o "$TOOLS/godot.zip" "https://github.com/godotengine/godot/releases/download/${GODOT_VERSION}-stable/Godot_v${GODOT_VERSION}-stable_linux.x86_64.zip"
  unzip -q -o "$TOOLS/godot.zip" -d "$TOOLS" && rm "$TOOLS/godot.zip"
fi
if ! command -v godot >/dev/null || ! godot --version | grep -q "^${GODOT_VERSION}"; then
  if [ -w "$BIN_DIR" ]; then ln -sf "$GODOT_BIN" "$BIN_DIR/godot"; else sudo ln -sf "$GODOT_BIN" "$BIN_DIR/godot"; fi
fi

# Export templates. The archive is ~1.2 GB; TEMPLATES keeps only what a CI job needs.
TPL="$HOME/.local/share/godot/export_templates/${GODOT_VERSION}.stable"
template_files() {
  case "$1" in
    web) echo "web_dlink_nothreads_debug.zip web_dlink_nothreads_release.zip" ;;
    android) echo "android_debug.apk android_release.apk" ;;
    linux) echo "linux_debug.x86_64 linux_release.x86_64" ;;
    windows) echo "windows_debug_x86_64.exe windows_release_x86_64.exe windows_debug_x86_64_console.exe windows_release_x86_64_console.exe" ;;
    *) echo "unknown template set: $1" >&2; exit 2 ;;
  esac
}
need_templates=0
if [ "$TEMPLATES" = "all" ]; then
  [ -f "$TPL/web_dlink_nothreads_release.zip" ] && [ -f "$TPL/android_debug.apk" ] && [ -f "$TPL/linux_release.x86_64" ] || need_templates=1
else
  for set in $TEMPLATES; do
    for f in $(template_files "$set"); do [ -f "$TPL/$f" ] || need_templates=1; done
  done
fi
if [ "$need_templates" = "1" ]; then
  mkdir -p "$TPL"
  curl -fsSL -o "$TOOLS/tpl.tpz" "https://github.com/godotengine/godot/releases/download/${GODOT_VERSION}-stable/Godot_v${GODOT_VERSION}-stable_export_templates.tpz"
  if [ "$TEMPLATES" = "all" ]; then
    unzip -q -o "$TOOLS/tpl.tpz" -d "$TOOLS/tpl" && mv -f "$TOOLS"/tpl/templates/* "$TPL/"
  else
    wanted=("templates/version.txt")
    for set in $TEMPLATES; do
      for f in $(template_files "$set"); do wanted+=("templates/$f"); done
    done
    unzip -q -o "$TOOLS/tpl.tpz" "${wanted[@]}" -d "$TOOLS/tpl" && mv -f "$TOOLS"/tpl/templates/* "$TPL/"
  fi
  rm -rf "$TOOLS/tpl" "$TOOLS/tpl.tpz"
fi

if [ "${EMSDK:-0}" = "1" ]; then
  if [ ! -x "$TOOLS/emsdk/upstream/emscripten/emcc" ]; then
    rm -rf "$TOOLS/emsdk"
    git clone -q --depth 1 --branch "$EMSDK_VERSION" https://github.com/emscripten-core/emsdk.git "$TOOLS/emsdk"
    "$TOOLS/emsdk/emsdk" install "$EMSDK_VERSION"
    "$TOOLS/emsdk/emsdk" activate "$EMSDK_VERSION"
    rm -rf "$TOOLS/emsdk/downloads"
  fi
  echo "web: run 'source $TOOLS/emsdk/emsdk_env.sh' before tools/export_web.sh"
fi

if [ "${ANDROID:-0}" = "1" ]; then
  export ANDROID_HOME="${ANDROID_HOME:-$TOOLS/android-sdk}"
  SDKMANAGER="$(command -v sdkmanager || true)"
  [ -n "$SDKMANAGER" ] || SDKMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"
  if [ ! -x "$SDKMANAGER" ]; then
    mkdir -p "$ANDROID_HOME/cmdline-tools"
    curl -fsSL -o "$TOOLS/cmdline-tools.zip" "https://dl.google.com/android/repository/commandlinetools-linux-${ANDROID_CMDLINE_TOOLS}_latest.zip"
    rm -rf "$TOOLS/cmdline-tools-tmp" "$ANDROID_HOME/cmdline-tools/latest"
    unzip -q -o "$TOOLS/cmdline-tools.zip" -d "$TOOLS/cmdline-tools-tmp"
    mv "$TOOLS/cmdline-tools-tmp/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest"
    rm -rf "$TOOLS/cmdline-tools-tmp" "$TOOLS/cmdline-tools.zip"
    SDKMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"
  fi
  if [ ! -d "$ANDROID_HOME/ndk/$ANDROID_NDK_VERSION" ] || [ ! -d "$ANDROID_HOME/build-tools/$ANDROID_BUILD_TOOLS" ] \
    || [ ! -d "$ANDROID_HOME/platform-tools" ] || [ ! -d "$ANDROID_HOME/platforms/$ANDROID_PLATFORM" ]; then
    (yes || true) | "$SDKMANAGER" --sdk_root="$ANDROID_HOME" --licenses >/dev/null
    "$SDKMANAGER" --sdk_root="$ANDROID_HOME" "platform-tools" "build-tools;$ANDROID_BUILD_TOOLS" \
      "platforms;$ANDROID_PLATFORM" "ndk;$ANDROID_NDK_VERSION" >/dev/null
  fi
  echo "android: export ANDROID_HOME=$ANDROID_HOME before tools/export_android.sh"
fi

if [ "${SKIP_SUBMODULE:-0}" != "1" ]; then
  git submodule update --init --depth 1 gdextension/godot-cpp
fi
echo "Orbitfall toolchain ready: $(godot --version)"
