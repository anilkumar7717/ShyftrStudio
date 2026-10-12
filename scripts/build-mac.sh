#!/usr/bin/env bash
# Build, test and (optionally) bundle Shyftr Studio on macOS.
#   ./scripts/build-mac.sh            Release build + tests
#   ./scripts/build-mac.sh --bundle   ... + macdeployqt (self-contained Qt frameworks)
#   ./scripts/build-mac.sh --debug    Debug build
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_TYPE=Release
BUNDLE=0
for a in "$@"; do
  case "$a" in
    --bundle) BUNDLE=1 ;;
    --debug) BUILD_TYPE=Debug ;;
  esac
done

need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing $1. Run: brew install $2"; exit 1; }; }
need brew homebrew
need cmake cmake
need ninja ninja
QT_PREFIX="$(brew --prefix qt 2>/dev/null || true)"
[ -d "$QT_PREFIX" ] || { echo "Qt not found. Run: brew install qt"; exit 1; }
command -v ffmpeg >/dev/null || echo "⚠ ffmpeg not found (brew install ffmpeg) — needed for import/export"
command -v whisper-cli >/dev/null || echo "⚠ whisper-cli not found (brew install whisper-cpp) — needed for automatic captions"

# --- Toolchain preflight -------------------------------------------------------
# A partially updated Command Line Tools install can ship a newer SDK than its
# linker understands ("tapi error: malformed file ... unknown architecture").
# Find an SDK the installed linker can actually link against.
link_ok() {  # $1 = SDK path ("" = default)
  local tmp; tmp="$(mktemp -d)"
  printf 'int main(){return 0;}\n' > "$tmp/t.cpp"
  if [ -n "$1" ]; then
    xcrun clang++ -isysroot "$1" "$tmp/t.cpp" -o "$tmp/t" >/dev/null 2>&1
  else
    xcrun clang++ "$tmp/t.cpp" -o "$tmp/t" >/dev/null 2>&1
  fi
  local rc=$?; rm -rf "$tmp"; return $rc
}
SYSROOT_ARG=()
if [ -n "${SHYFTR_SDK:-}" ]; then
  SYSROOT_ARG=(-DCMAKE_OSX_SYSROOT="$SHYFTR_SDK")
elif ! link_ok ""; then
  echo "⚠ The default macOS SDK can't be linked with this toolchain (SDK newer than the linker)."
  FOUND=""
  for sdk in $(ls -d "$(xcode-select -p)"/SDKs/MacOSX[0-9]*.sdk \
                   "$(xcode-select -p)"/Platforms/MacOSX.platform/Developer/SDKs/MacOSX[0-9]*.sdk 2>/dev/null | sort -rV); do
    if link_ok "$sdk"; then FOUND="$sdk"; break; fi
  done
  if [ -z "$FOUND" ]; then
    echo "✗ No installed SDK links. Repair the Command Line Tools:"
    echo "    sudo rm -rf /Library/Developer/CommandLineTools && xcode-select --install"
    echo "  (or install the latest Xcode and run: sudo xcode-select -s /Applications/Xcode.app)"
    exit 1
  fi
  echo "  → using $FOUND"
  SYSROOT_ARG=(-DCMAKE_OSX_SYSROOT="$FOUND")
  rm -rf build/CMakeCache.txt build/CMakeFiles   # forget the failed compiler check
fi

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_PREFIX_PATH="$QT_PREFIX" "${SYSROOT_ARG[@]}"
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure

if [ "$BUNDLE" = 1 ]; then
  "$QT_PREFIX/bin/macdeployqt" "build/Shyftr Studio.app" -always-overwrite
  echo "Bundled. To ship engines inside the app, copy ffmpeg, ffprobe and whisper-cli (plus their dylibs) into"
  echo "  build/Shyftr Studio.app/Contents/Resources/bin/   and sign/notarize together with the app."
fi
echo
echo "✓ Built: build/Shyftr Studio.app"
echo "  Run:   open \"build/Shyftr Studio.app\""
