# Troubleshooting

| Symptom | Fix |
|---|---|
| "FFmpeg was not found" | `brew install ffmpeg` (macOS), or set the path in Preferences → Tools → *Check tools*. |
| "The local speech engine … was not found" | `brew install whisper-cpp` (provides `whisper-cli`), or set its path in Preferences → Tools. |
| "No speech model is installed" | Run `./scripts/get-model.sh small`, then Preferences → AI Models → Add model file…. |
| Model "changed" / checksum mismatch | The file was modified after import. Remove it and re-add it from a trusted copy. |
| Captions in the wrong script for Hinglish | Choose *Hinglish (Roman script)*. Use a larger model (small/medium) and add common words to Vocabulary. |
| Transcription is slow | Use a smaller model, enable GPU (Preferences → Performance), or set AI threads. |
| Preview stutters | Viewer → *Preview: Half/Quarter*. Export always renders at full quality. |
| Media offline (red hatching) | File → Relink Media → *Search folder…*. |
| App closed unexpectedly | Reopen the project and accept *Recover unsaved changes*. Backups are in `backups/`, autosaves in `autosaves/`. |
| Export failed | Click *Open error log* in the Export Center. The log and the exact render graph are in `exports/logs/`. |
| Hardware encoder errors | Preferences → Performance → *Software only*. |
| Microphone not listed (macOS) | System Settings → Privacy & Security → Microphone → enable Shyftr Studio. |
| "Already open in another window" | Close the other window. A lock left by a crashed process is cleared automatically. |
