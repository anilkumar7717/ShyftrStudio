# Testing

## Automated (`ctest --test-dir build --output-on-failure`)
| Suite | Covers |
|---|---|
| `tst_model` | Rational frame maths with NTSC rates (no drift over an hour), snapping, timecode, clip time mapping |
| `tst_timeline` | Linked import, overlap prevention, split continuity and link groups, trim clamping, move clamping and track changes, ripple delete, speed limits, snapping, undo/redo and merged slider edits, rejection of invalid edits |
| `tst_project_io` | Lossless round trip, validation, refusal of newer schemas, **v1→v2 migration**, atomic save + backup rotation, **recovery from a torn project.json**, refusing invalid saves, autosave rotation + crash detection, relink search, collect and package, settings and shortcut persistence and conflicts |
| `tst_captions` | 15–20 templates across all six families, overrides and brand presets, line breaking, segmentation (pauses, sentences, limits, manual cues, hard breaks), whisper.cpp JSON parsing (sub-words, punctuation, markers, Devanagari, confidence), alignment refinement, PCM envelope, **caption anchoring through move/trim/split+ripple/speed**, transcript editing, SRT/VTT/ASS writers and SRT/VTT reader, range clipping |
| `tst_render_export` | Every template renders inside the frame (9:16), highlight colouring, **colour preview ≡ FFmpeg (mean error < 2/255)**, geometry fit/fill/rotate/crop, ffprobe + waveform peaks, **overlay export ≡ preview pixels**, full multitrack export (mixed 30/24 fps, vertical source, image, speed, rotation, fades, colour) with exact frame counts, range export, **cancel leaves no partial file** |
| `tst_fx` | Keyframe easing and per-type mapping, speed curves (duration kept, monotonic, invertible), split keeps keyframed motion, every new property survives save/load, chroma key, masks (incl. invert), every effect and transition runs and transitions start on A and end on B, looks, LUT identity, voice-effect chains |
| `tst_tools` | Ripple removal of source ranges (linked audio follows), filler-word ranges, freeze-frame insert, beat onset detection, FFmpeg silence and scene detection, reverse (first/last frames swap), extract audio, stabilise, and local text-to-speech when a speech engine exists |
| `tst_render_export` (new cases) | **Compositor export ≡ filter-graph export**, **exported frames ≡ compositor frames** with transitions, keyframes, effects, filters, canvas blur and an effect layer, audio-only (WAV/M4A with voice effect + loudness) and GIF export |
| `tst_transcription` | Language presets, WAV reader, **model import with checksum, verification and tamper detection**, clear missing-model errors, and an optional end-to-end whisper run |

End-to-end transcription with a real model:
```bash
SHYFTR_TEST_MODEL=~/ShyftrStudio/AI_Models/ggml-small.bin \
SHYFTR_TEST_AUDIO=/path/to/clip.wav SHYFTR_TEST_LANG=hi \
ctest --test-dir build -R tst_transcription --output-on-failure
```

## Language evaluation (English, Hindi, Hinglish, Punjabi)
The plan asks for these languages to be validated on representative recordings. Put clips and reference transcripts in a folder like this:
```
samples/en/clip1.wav + clip1.txt
samples/hi/...   samples/hinglish/...   samples/pa/...
```
then run:
```bash
python3 scripts/eval_transcription.py samples --model ~/ShyftrStudio/AI_Models/ggml-small.bin
```
It reports word error rate, real-time factor and low-confidence share per language. Use it to choose default models and set quality thresholds after baseline runs, as the plan recommends, rather than promising numbers in advance.

## Release acceptance checklist (from plan §11)
- [ ] **Fully offline:** turn off Wi-Fi and block outbound traffic, then import → edit → transcribe → export.
- [ ] **No data egress:** a network monitor (e.g. Little Snitch / LuLu) shows no connections from Shyftr Studio, ffmpeg or whisper-cli.
- [ ] **Persistence:** timeline, titles, audio levels and caption edits survive a reopen.
- [ ] **Recovery:** `kill -9` during a save or export, reopen, and the last valid project plus the autosave are offered.
- [ ] **Captions:** words and timing are editable, and the template looks the same in preview and export.
- [ ] **A/V sync:** mixed 24/25/30/60 fps and VFR phone clips, plus a one-hour clip, show no drift at the end.
- [ ] **Formats:** H.264/H.265/AAC/PCM MP4 and MOV pass the media matrix.
- [ ] **Hardware:** core editing is usable on an 8 GB machine with Preview: Quarter.
- [ ] **Documentation:** offline setup, model install, known limits, recovery, troubleshooting, privacy.
