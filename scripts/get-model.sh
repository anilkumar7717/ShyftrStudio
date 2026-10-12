#!/usr/bin/env bash
# Downloads a whisper.cpp speech model ONCE (the only online step), then prints its SHA-256.
# Add it in the app: Preferences → AI Models → Add model file…  (it is copied into AI_Models and verified).
#   ./scripts/get-model.sh small        tiny | base | small | medium | large-v3-turbo | large-v3 | vad
set -euo pipefail
MODEL="${1:-small}"
DEST="${2:-$HOME/Downloads}"
mkdir -p "$DEST"
if [ "$MODEL" = "vad" ]; then
  FILE="ggml-silero-v5.1.2.bin"
  URL="https://huggingface.co/ggml-org/whisper-vad/resolve/main/$FILE"
else
  FILE="ggml-$MODEL.bin"
  URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$FILE"
fi
echo "Downloading $FILE → $DEST"
curl -L --fail --progress-bar -o "$DEST/$FILE.part" "$URL"
MAGIC="$(head -c 4 "$DEST/$FILE.part")"
if [ "$MAGIC" != "lmgg" ] && [ "$MAGIC" != "GGUF" ]; then
  echo "✗ The download is not a whisper model (got: $(head -c 40 "$DEST/$FILE.part" | tr -d '\n'))"
  rm -f "$DEST/$FILE.part"; exit 1
fi
mv "$DEST/$FILE.part" "$DEST/$FILE"
echo "Size: $(du -h "$DEST/$FILE" | cut -f1)"
echo "SHA-256: $(shasum -a 256 "$DEST/$FILE" | cut -d' ' -f1)"
echo "Now add it in Shyftr Studio: Preferences → AI Models → Add model file… → $DEST/$FILE"
