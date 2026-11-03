#!/usr/bin/env bash
# Downloads / builds everything Shyftr Studio needs at run time on macOS (Apple Silicon), into .deps/ :
#   .deps/ffmpeg/    ffmpeg, ffprobe              (Martin Riedl's static arm64 build: x264, x265, lame, ...)
#   .deps/whisper/   whisper-cli                  (built from whisper.cpp source: static, Metal GPU, portable CPU code)
#   .deps/models/    ggml-*.bin speech models     (+ .sha256 files, checked against Hugging Face)
#   .deps/jfk.wav    sample used by the package self-test
# Used by the GitHub workflow .github/workflows/macos.yml. Safe to re-run: finished parts are skipped.
#
#   ./scripts/macos/fetch-deps.sh [--models small,base] [--deps .deps] [--whisper-version v1.9.5] [--no-vad]
#
# --models: any of tiny, base, small, medium, large-v3-turbo, large-v3 (comma separated), or none.
#           "small" is the recommended default for Hindi / Hinglish / Punjabi (~466 MB).
# Needs: Xcode command line tools, cmake, ninja (brew install cmake ninja), python3, curl, git.
# Override the FFmpeg download with FFMPEG_ZIP_URL / FFPROBE_ZIP_URL if the default server is down.
set -euo pipefail
cd "$(dirname "$0")/../.."

MODELS="small"
DEPS=".deps"
WHISPER_VERSION="v1.9.5"
VAD=1
DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-14.0}"
while [ $# -gt 0 ]; do
  case "$1" in
    --models) MODELS="$2"; shift 2 ;;
    --deps) DEPS="$2"; shift 2 ;;
    --whisper-version) WHISPER_VERSION="$2"; shift 2 ;;
    --no-vad) VAD=0; shift ;;
    *) echo "Unknown option $1"; exit 2 ;;
  esac
done
mkdir -p "$DEPS"
DEPS="$(cd "$DEPS" && pwd)"
MODELS="$(echo "$MODELS" | tr 'A-Z' 'a-z' | tr -d ' ')"
[ "$MODELS" = "none" ] && MODELS=""

step() { printf '\n\033[36m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33m  %s\033[0m\n' "$*"; }
download() {  # url out
  curl -fL --retry 4 --retry-delay 5 --retry-all-errors -A "ShyftrStudio-build" -o "$2" "$1"
}
sha256() { shasum -a 256 "$1" | awk '{print $1}'; }

[ "$(uname -s)" = Darwin ] || { echo "This script is for macOS."; exit 1; }
ARCH="$(uname -m)"
[ "$ARCH" = arm64 ] || { echo "Only Apple Silicon (arm64) is supported (the FFmpeg builds used here are arm64 only)."; exit 1; }

# ------------------------------------------------------------------------------------------ FFmpeg
step "FFmpeg (static, arm64)"
FF="$DEPS/ffmpeg"
if [ -x "$FF/ffmpeg" ] && [ -x "$FF/ffprobe" ]; then
  echo "  already present: $FF"
else
  mkdir -p "$FF"
  base="https://ffmpeg.martin-riedl.de/redirect/latest/macos/arm64/release"
  for tool in ffmpeg ffprobe; do
    if [ "$tool" = ffmpeg ]; then url="${FFMPEG_ZIP_URL:-$base/ffmpeg.zip}"; else url="${FFPROBE_ZIP_URL:-$base/ffprobe.zip}"; fi
    zip="$DEPS/$tool.zip"
    # follow the "latest" redirect; remember the concrete build URL to fetch its checksum file
    real="$(curl -fL --retry 4 --retry-delay 5 --retry-all-errors -A "ShyftrStudio-build" -o "$zip" -w '%{url_effective}' "$url")"
    echo "  $real"
    if curl -fsSL --retry 3 -o "$zip.sha256" "$real.sha256" 2>/dev/null && grep -qiE '[0-9a-f]{64}' "$zip.sha256"; then
      want="$(grep -oiE '[0-9a-f]{64}' "$zip.sha256" | head -1 | tr 'A-F' 'a-f')"
      got="$(sha256 "$zip")"
      [ "$want" = "$got" ] || { echo "✗ $tool download is corrupted (SHA-256 $got, expected $want)"; exit 1; }
      echo "  sha256 ok  $got"
    else
      warn "no checksum published for $tool; continuing (HTTPS download)"
    fi
    tmp="$DEPS/$tool-extract"; rm -rf "$tmp"; mkdir -p "$tmp"
    unzip -q -o "$zip" -d "$tmp"
    bin="$(find "$tmp" -type f -name "$tool" | head -1)"
    [ -n "$bin" ] || { echo "✗ $tool not found inside $zip"; exit 1; }
    cp "$bin" "$FF/$tool"; chmod 755 "$FF/$tool"
    rm -rf "$tmp" "$zip" "$zip.sha256"
  done
  xattr -cr "$FF" 2>/dev/null || true
  cat > "$FF/FFMPEG-LICENSE.txt" <<'EOF'
The bundled ffmpeg and ffprobe programs are FFmpeg (https://ffmpeg.org), built with --enable-gpl
--enable-version3 by Martin Riedl (https://ffmpeg.martin-riedl.de, build scripts:
https://git.martin-riedl.de/ffmpeg/build-script). They are licensed under the GNU GPL version 3.
Source code: https://ffmpeg.org/download.html
EOF
fi
"$FF/ffmpeg" -hide_banner -version | head -1
# Check the filters and encoders the editor relies on are really in this build (same list as Windows).
filters="$("$FF/ffmpeg" -hide_banner -filters 2>&1)"
encoders="$("$FF/ffmpeg" -hide_banner -encoders 2>&1)"
missing=(); fatal=()
for f in silencedetect loudnorm afftdn sidechaincompress afftfilt aecho palettegen paletteuse \
         vidstabdetect vidstabtransform deshake select showinfo atempo amix; do
  grep -qE "[[:space:]]$f[[:space:]]" <<<"$filters" || missing+=("filter $f")
done
for e in libx264 libx265 aac libmp3lame flac gif png pcm_s16le h264_videotoolbox hevc_videotoolbox; do
  grep -qE "[[:space:]]$e[[:space:]]" <<<"$encoders" || missing+=("encoder $e")
done
for m in "${missing[@]+"${missing[@]}"}"; do
  case "$m" in   # the app has fallbacks for these
    "filter vidstabdetect"|"filter vidstabtransform"|"encoder libx265"|"encoder h264_videotoolbox"|"encoder hevc_videotoolbox") ;;
    *) fatal+=("$m") ;;
  esac
done
[ ${#missing[@]} -eq 0 ] || warn "not in this FFmpeg build: ${missing[*]}"
[ ${#fatal[@]} -eq 0 ] || { echo "✗ FFmpeg build lacks required features: ${fatal[*]}"; exit 1; }

# ------------------------------------------------------------------------------------- whisper-cli
step "whisper.cpp $WHISPER_VERSION (whisper-cli)"
W="$DEPS/whisper"
if [ -x "$W/whisper-cli" ] && [ "$(cat "$W/VERSION.txt" 2>/dev/null)" = "$WHISPER_VERSION" ] && [ -f "$DEPS/jfk.wav" ]; then
  echo "  already present: $W"
else
  src="$DEPS/whisper-src"; bld="$DEPS/whisper-build"
  rm -rf "$src" "$bld"
  git clone --quiet --depth 1 --branch "$WHISPER_VERSION" https://github.com/ggml-org/whisper.cpp "$src"
  gen=(); command -v ninja >/dev/null && gen=(-G Ninja)
  # Static (one self-contained executable). Metal GPU with the shaders embedded in the binary.
  # GGML_NATIVE=OFF: portable code for every Apple Silicon Mac, not just the build machine.
  # GGML_OPENMP=OFF: Apple's compiler has no OpenMP; a Homebrew libomp would not exist on users' Macs.
  cmake -S "$src" -B "$bld" ${gen[@]+"${gen[@]}"} -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET" \
        -DBUILD_SHARED_LIBS=OFF -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF \
        -DGGML_METAL=ON -DGGML_METAL_EMBED_LIBRARY=ON -DGGML_BLAS=ON \
        -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_SERVER=OFF -DWHISPER_BUILD_EXAMPLES=ON -DWHISPER_SDL2=OFF
  cmake --build "$bld" --config Release --target whisper-cli -j "$(sysctl -n hw.ncpu)"
  exe="$(find "$bld" -type f -name whisper-cli -perm -u+x | head -1)"
  [ -n "$exe" ] || { echo "✗ whisper-cli was not built"; exit 1; }
  mkdir -p "$W"
  cp "$exe" "$W/whisper-cli"
  cp "$src/LICENSE" "$W/WHISPER-LICENSE.txt"
  cp "$src/samples/jfk.wav" "$DEPS/jfk.wav"
  echo "$WHISPER_VERSION" > "$W/VERSION.txt"
  rm -rf "$bld" "$src"
fi
# must not depend on anything outside macOS itself (Homebrew etc.)
if otool -L "$W/whisper-cli" | tail -n +2 | awk '{print $1}' | grep -vE '^(/System/|/usr/lib/)'; then
  echo "✗ whisper-cli links libraries that will not exist on users' Macs (above)"; exit 1
fi
"$W/whisper-cli" --help 2>&1 | head -2 || true

# ------------------------------------------------------------------------------------ speech models
step "Speech models: ${MODELS:-none}"
M="$DEPS/models"
mkdir -p "$M"
known=" tiny base small medium large-v3-turbo large-v3 tiny.en base.en small.en medium.en "

hf_hashes() {  # repo -> lines "file sha size"
  curl -fsSL --retry 3 --retry-all-errors "https://huggingface.co/api/models/$1/tree/main" 2>/dev/null |
    python3 -c 'import json,sys
for e in json.load(sys.stdin):
    if e.get("lfs"): print(e["path"], e["lfs"]["oid"], e["lfs"]["size"])' 2>/dev/null ||
    warn "(could not read checksums from Hugging Face for $1)"
}
get_model() {  # repo file minBytes hashLines
  local repo="$1" file="$2" min="$3" hashes="$4" out="$M/$2"
  local want wsize
  want="$(awk -v f="$file" '$1==f {print $2}' <<<"$hashes")"
  wsize="$(awk -v f="$file" '$1==f {print $3}' <<<"$hashes")"
  if [ -f "$out" ] && [ -f "$out.sha256" ] && { [ -z "$wsize" ] || [ "$(stat -f%z "$out")" = "$wsize" ]; }; then
    echo "  already present: $file"; return 0
  fi
  echo "  downloading $file ..."
  download "https://huggingface.co/$repo/resolve/main/$file" "$out.part"
  local len magic sha
  len="$(stat -f%z "$out.part")"
  magic="$(head -c 4 "$out.part")"
  if { [ "$magic" != lmgg ] && [ "$magic" != GGUF ]; } || [ "$len" -lt "$min" ]; then
    rm -f "$out.part"; echo "✗ $file is not a valid whisper.cpp model ($len bytes, header '$magic')"; return 1
  fi
  sha="$(sha256 "$out.part")"
  if [ -n "$want" ] && [ "$sha" != "$want" ]; then
    rm -f "$out.part"; echo "✗ $file SHA-256 mismatch (got $sha, expected $want)"; return 1
  fi
  mv -f "$out.part" "$out"
  printf '%s  %s' "$sha" "$file" > "$out.sha256"
  echo "  ok  $file  $((len / 1048576)) MB  sha256 $sha"
}

if [ -n "$MODELS" ]; then
  hashes="$(hf_hashes ggerganov/whisper.cpp)"
  IFS=',' read -r -a list <<<"$MODELS"
  for m in "${list[@]}"; do
    [ -n "$m" ] || continue
    case "$known" in *" $m "*) ;; *) echo "✗ Unknown model '$m'. Choose from:$known"; exit 1 ;; esac
    get_model ggerganov/whisper.cpp "ggml-$m.bin" $((20 * 1048576)) "$hashes"
  done
fi
if [ "$VAD" = 1 ]; then
  # Optional voice-activity-detection model (tiny); improves timing on long silences.
  get_model ggml-org/whisper-vad ggml-silero-v5.1.2.bin 102400 "$(hf_hashes ggml-org/whisper-vad)" ||
    warn "VAD model skipped"
fi
# remove models that are no longer requested, so the package only carries what was asked for
for f in "$M"/ggml-*.bin; do
  [ -e "$f" ] || continue
  n="$(basename "$f" .bin)"; n="${n#ggml-}"
  case "$n" in silero*) continue ;; esac
  case ",$MODELS," in *",$n,"*) ;; *) echo "  removing unrequested $(basename "$f")"; rm -f "$f" "$f.sha256" ;; esac
done

step "Dependencies ready in $DEPS"
ls -la "$FF" "$W" "$M"
