#!/usr/bin/env bash
# Assembles the self-contained macOS app, checks it really works, and wraps it in a .dmg:
#   dist/Shyftr Studio.app            app + Qt frameworks + FFmpeg + whisper-cli + speech models
#   dist/ShyftrStudio-macos-arm64.dmg drag-to-Applications disk image
# Run after building (cmake --build build) and scripts/macos/fetch-deps.sh:
#   ./scripts/macos/package.sh --qt-dir ~/Qt/6.8.3/macos [--version 1.2.3]
#
# Signing (optional): with a "Developer ID Application" certificate in the keychain the app and the
# disk image are signed with it (hardened runtime). Set MACOS_SIGN_IDENTITY to pick a certificate.
# Notarization (optional, needs the certificate): set APPLE_ID, APPLE_TEAM_ID and APPLE_APP_PASSWORD
# (an app-specific password from account.apple.com). Without a certificate the app is signed
# "ad hoc": it runs, but on first open macOS asks the user to allow it (see the note in the .dmg).
set -euo pipefail
cd "$(dirname "$0")/../.."

QT_DIR="${QT_ROOT_DIR:-}"
BUILD_DIR="build"
DEPS=".deps"
DIST="dist"
VERSION=""
SELFTEST=1
while [ $# -gt 0 ]; do
  case "$1" in
    --qt-dir) QT_DIR="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --deps) DEPS="$2"; shift 2 ;;
    --dist) DIST="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --no-selftest) SELFTEST=0; shift ;;
    *) echo "Unknown option $1"; exit 2 ;;
  esac
done

step() { printf '\n\033[36m==> %s\033[0m\n' "$*"; }
die() { echo "✗ $*" >&2; exit 1; }

APP_NAME="Shyftr Studio"
SRC_APP="$BUILD_DIR/$APP_NAME.app"
[ -d "$SRC_APP" ] || die "$SRC_APP not found - build the app first."
[ -x "$QT_DIR/bin/macdeployqt" ] || die "macdeployqt not found under --qt-dir '$QT_DIR'"
for f in ffmpeg/ffmpeg ffmpeg/ffprobe whisper/whisper-cli; do
  [ -x "$DEPS/$f" ] || die "$DEPS/$f missing - run scripts/macos/fetch-deps.sh first."
done
mkdir -p "$DIST"
DEPS="$(cd "$DEPS" && pwd)"
DIST="$(cd "$DIST" && pwd)"
APP="$DIST/$APP_NAME.app"
ARCH="$(uname -m)"
DMG="$DIST/ShyftrStudio-macos-$ARCH.dmg"
PLIST="$APP/Contents/Info.plist"

# -------------------------------------------------------------------------------------- assemble
step "Assembling $APP"
rm -rf "$APP" "$DMG"
ditto "$SRC_APP" "$APP"

# Qt frameworks and plugins (platform, styles, image formats, multimedia + Qt's own FFmpeg libraries)
"$QT_DIR/bin/macdeployqt" "$APP" -always-overwrite -verbose=1

# Qt Multimedia's FFmpeg backend needs Qt's FFmpeg dylibs; older macdeployqt versions miss them.
if ls "$APP/Contents/PlugIns/multimedia/"*ffmpeg* >/dev/null 2>&1 && ! ls "$APP/Contents/Frameworks/"libavcodec* >/dev/null 2>&1; then
  for lib in "$QT_DIR"/lib/libav*.dylib "$QT_DIR"/lib/libsw*.dylib; do
    [ -e "$lib" ] && cp -a "$lib" "$APP/Contents/Frameworks/"
  done
fi

# Local engines: FFmpeg and whisper.cpp (the app looks in Contents/Resources/bin first)
mkdir -p "$APP/Contents/Resources/bin" "$APP/Contents/Resources/licenses"
cp "$DEPS/ffmpeg/ffmpeg" "$DEPS/ffmpeg/ffprobe" "$DEPS/whisper/whisper-cli" "$APP/Contents/Resources/bin/"
chmod 755 "$APP/Contents/Resources/bin/"*
cp "$DEPS/ffmpeg/FFMPEG-LICENSE.txt" "$DEPS/whisper/WHISPER-LICENSE.txt" "$APP/Contents/Resources/licenses/" 2>/dev/null || true

# Speech models (Contents/Resources/models, read-only; the app lists them as "Included with Shyftr Studio")
if ls "$DEPS"/models/ggml-*.bin >/dev/null 2>&1; then
  mkdir -p "$APP/Contents/Resources/models"
  cp "$DEPS"/models/ggml-*.bin "$APP/Contents/Resources/models/"
  cp "$DEPS"/models/ggml-*.bin.sha256 "$APP/Contents/Resources/models/" 2>/dev/null || true
fi

# Qt ships universal (Intel + Apple Silicon) frameworks; keep only this Mac's architecture (~halves their size)
ditto --arch "$ARCH" "$APP" "$APP.thin"
rm -rf "$APP"; mv "$APP.thin" "$APP"

if [ -n "$VERSION" ]; then
  /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$PLIST"
  /usr/libexec/PlistBuddy -c "Set :CFBundleVersion $VERSION" "$PLIST"
fi
VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$PLIST")"
echo "  version $VERSION"

# --------------------------------------------------------------------------- dependency check
step "Checking every binary only uses what is inside the app or part of macOS"
bad=0
macho_files() {
  find "$APP/Contents" -type f \( -perm -u+x -o -name '*.dylib' -o -name '*.so' \) -print0 |
    while IFS= read -r -d '' f; do
      file -b "$f" | grep -q 'Mach-O' && printf '%s\0' "$f"
    done
}
while IFS= read -r -d '' f; do
  dir="$(dirname "$f")"
  self_id="$(otool -arch "$ARCH" -D "$f" | tail -n +2 | head -1)"   # a library's own install name is not a dependency
  while read -r dep; do
    [ -n "$dep" ] && [ "$dep" != "$self_id" ] || continue
    case "$dep" in
      /System/*|/usr/lib/*) ;;
      @rpath/*)
        rest="${dep#@rpath/}"
        [ -e "$APP/Contents/Frameworks/$rest" ] || [ -e "$dir/$rest" ] || { echo "  $f -> $dep (not in app)"; bad=1; } ;;
      @executable_path/*)
        [ -e "$APP/Contents/MacOS/${dep#@executable_path/}" ] || { echo "  $f -> $dep (not in app)"; bad=1; } ;;
      @loader_path/*)
        [ -e "$dir/${dep#@loader_path/}" ] || { echo "  $f -> $dep (not in app)"; bad=1; } ;;
      *) echo "  $f -> $dep (outside the app)"; bad=1 ;;
    esac
  done < <(otool -arch "$ARCH" -L "$f" | tail -n +2 | awk '{print $1}')
done < <(macho_files)
[ "$bad" = 0 ] || die "The app depends on files users won't have (listed above)."
for f in "Contents/MacOS/$APP_NAME" Contents/Frameworks/QtCore.framework Contents/Frameworks/QtWidgets.framework \
         Contents/Frameworks/QtMultimedia.framework Contents/PlugIns/platforms/libqcocoa.dylib \
         Contents/Resources/bin/ffmpeg Contents/Resources/bin/ffprobe Contents/Resources/bin/whisper-cli; do
  [ -e "$APP/$f" ] || die "Missing $f in the app"
done
echo "  ok"

# ------------------------------------------------------------------------------------- signing
step "Signing"
xattr -cr "$APP"
IDENTITY="${MACOS_SIGN_IDENTITY:-}"
if [ -z "$IDENTITY" ]; then
  IDENTITY="$(security find-identity -v -p codesigning 2>/dev/null | grep -o '"Developer ID Application:[^"]*"' | head -1 | tr -d '"' || true)"
fi
ENTITLEMENTS="$(pwd)/installer/macos/entitlements.plist"
if [ -n "$IDENTITY" ]; then
  echo "  identity: $IDENTITY"
  sign=(codesign --force --timestamp --options runtime --sign "$IDENTITY")
else
  echo "  no Developer ID certificate found -> ad-hoc signature (users confirm on first open)"
  sign=(codesign --force --sign -)
fi
# inside-out: loose binaries and libraries, then frameworks, then the app itself
while IFS= read -r -d '' f; do
  case "$f" in *.framework/*) continue ;; esac
  "${sign[@]}" "$f"
done < <(macho_files)
find "$APP/Contents/Frameworks" -maxdepth 1 -name '*.framework' -print0 | while IFS= read -r -d '' fw; do
  "${sign[@]}" "$fw"
done
if [ -n "$IDENTITY" ]; then
  "${sign[@]}" --entitlements "$ENTITLEMENTS" "$APP"
else
  "${sign[@]}" "$APP"
fi
codesign --verify --deep --strict --verbose=2 "$APP"

# ------------------------------------------------------------------------------------ self-test
if [ "$SELFTEST" = 1 ]; then
  step "Self-test of the bundled engines"
  BIN="$APP/Contents/Resources/bin"
  "$BIN/ffmpeg" -hide_banner -version | head -1
  "$BIN/ffprobe" -hide_banner -version | head -1
  tmp="$(mktemp -d)"
  "$BIN/ffmpeg" -hide_banner -loglevel error -f lavfi -i testsrc2=size=640x360:rate=30 -f lavfi -i sine=f=440 \
      -t 2 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$tmp/test.mp4"
  "$BIN/ffprobe" -v error -show_entries format=duration -of csv=p=0 "$tmp/test.mp4"
  model="$(ls "$APP"/Contents/Resources/models/ggml-*.bin 2>/dev/null | grep -v silero | head -1 || true)"
  if [ -n "$model" ] && [ -f "$DEPS/jfk.wav" ]; then
    echo "  transcribing a sample with $(basename "$model") ..."
    # -ng: CI machines have no usable GPU; on users' Macs whisper uses Metal
    "$BIN/whisper-cli" -m "$model" -f "$DEPS/jfk.wav" -ng -nt -l en -of "$tmp/jfk" -otxt >/dev/null 2>"$tmp/whisper.log" ||
      { cat "$tmp/whisper.log"; die "whisper-cli failed"; }
    text="$(cat "$tmp/jfk.txt")"; echo "  heard: $text"
    grep -qi "country" <<<"$text" || die "whisper transcription looks wrong"
  else
    echo "  (no speech model bundled - transcription test skipped)"
  fi
  rm -rf "$tmp"
fi

# -------------------------------------------------------------------------------------- notarize
NOTARIZE=0
if [ -n "$IDENTITY" ] && [ -n "${APPLE_ID:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ] && [ -n "${APPLE_APP_PASSWORD:-}" ]; then
  NOTARIZE=1
fi
notarize() {  # file
  xcrun notarytool submit "$1" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
        --password "$APPLE_APP_PASSWORD" --wait --timeout 60m 2>&1 | tee "$DIST/notary.log"
  grep -q 'status: Accepted' "$DIST/notary.log" || {
    id="$(grep -m1 -o 'id: [0-9a-f-]*' "$DIST/notary.log" | awk '{print $2}')"
    [ -n "$id" ] && xcrun notarytool log "$id" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" || true
    die "Notarization of $(basename "$1") was rejected (details above)"
  }
}
if [ "$NOTARIZE" = 1 ]; then
  step "Notarizing the app (so it also opens without an internet connection)"
  ditto -c -k --keepParent "$APP" "$DIST/app-notarize.zip"
  notarize "$DIST/app-notarize.zip"
  xcrun stapler staple "$APP"
  rm -f "$DIST/app-notarize.zip"
fi

# ------------------------------------------------------------------------------------------- dmg
step "Building $(basename "$DMG")"
stage="$DIST/dmg-stage"
rm -rf "$stage"; mkdir -p "$stage"
ditto "$APP" "$stage/$APP_NAME.app"
ln -s /Applications "$stage/Applications"
if [ "$NOTARIZE" = 0 ]; then
  cat > "$stage/If macOS blocks the app.txt" <<'EOF'
Installing Shyftr Studio
========================
1. Drag "Shyftr Studio" onto the Applications folder in this window.
2. Open Shyftr Studio from Applications.

This build is not notarized by Apple yet, so the first time you open it macOS says it
"cannot verify" the app. To allow it (only needed once):

  - Click "Done" (or "OK") in that message.
  - Open System Settings > Privacy & Security, scroll down, and click "Open Anyway"
    next to the message about Shyftr Studio. Confirm with your password.

Or, in Terminal:   xattr -dr com.apple.quarantine "/Applications/Shyftr Studio.app"

Everything runs on your Mac: video, captions and speech recognition never leave it.
EOF
fi
ok=0
for i in 1 2 3 4 5; do   # hdiutil is occasionally "Resource busy" on CI machines
  if hdiutil create -volname "$APP_NAME" -srcfolder "$stage" -fs HFS+ -format ULFO -ov "$DMG"; then ok=1; break; fi
  echo "  hdiutil failed (attempt $i), retrying ..."; sleep $((i * 5))
done
[ "$ok" = 1 ] || die "hdiutil could not create the disk image"
rm -rf "$stage"
if [ -n "$IDENTITY" ]; then
  codesign --force --timestamp --sign "$IDENTITY" "$DMG"
fi
if [ "$NOTARIZE" = 1 ]; then
  step "Notarizing the disk image"
  notarize "$DMG"
  xcrun stapler staple "$DMG"
  spctl -a -t open --context context:primary-signature -vv "$DMG"
fi
hdiutil verify "$DMG"

# ------------------------------------------------------------- install test from the disk image
if [ "$SELFTEST" = 1 ]; then
  step "Install test: mount the .dmg, copy the app, start it"
  mnt="$(mktemp -d)/dmg"; mkdir -p "$mnt"
  hdiutil attach -nobrowse -readonly -mountpoint "$mnt" "$DMG" >/dev/null
  dest="$(mktemp -d)"
  ditto "$mnt/$APP_NAME.app" "$dest/$APP_NAME.app"
  hdiutil detach "$mnt" -quiet || hdiutil detach "$mnt" -force -quiet || true
  for f in ffmpeg ffprobe whisper-cli; do
    [ -x "$dest/$APP_NAME.app/Contents/Resources/bin/$f" ] || die "Installed app lacks $f"
  done
  codesign --verify --deep --strict "$dest/$APP_NAME.app"
  root="$(mktemp -d)"
  SHYFTR_STUDIO_ROOT="$root" "$dest/$APP_NAME.app/Contents/MacOS/$APP_NAME" >"$root/app.log" 2>&1 &
  pid=$!
  sleep 12
  if ! kill -0 "$pid" 2>/dev/null; then
    wait "$pid" || true
    cat "$root/app.log"
    die "The installed app exited at start-up"
  fi
  kill "$pid" 2>/dev/null || true; sleep 1; kill -9 "$pid" 2>/dev/null || true
  echo "  app started and kept running"
  rm -rf "$dest" "$root"
fi

step "Done"
du -sh "$APP" "$DMG"
