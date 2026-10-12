#!/usr/bin/env python3
"""Offline transcription evaluation for Shyftr Studio's priority languages.

Layout:
    samples/<lang>/<name>.wav|.mp4|.m4a   audio/video with speech
    samples/<lang>/<name>.txt             reference transcript (plain text)
<lang> is one of: en, hi, hinglish, pa (others are passed straight to whisper).

Usage:
    python3 scripts/eval_transcription.py samples --model ~/ShyftrStudio/AI_Models/ggml-small.bin
Reports word error rate (WER), real-time factor and low-confidence word share per language,
using the same whisper-cli options and language prompts as the app.
"""
import argparse, json, os, re, shutil, subprocess, sys, tempfile, time, unicodedata

LANG = {  # mirrors src/captions/Transcription.cpp
    "en": ("en", ""),
    "hi": ("hi", "नमस्ते, आज हम बात करेंगे।"),
    "hinglish": ("hi", "Haan bhai, aaj hum baat karenge. Toh chalo shuru karte hain, theek hai? This is really important."),
    "pa": ("pa", "ਸਤ ਸ੍ਰੀ ਅਕਾਲ ਜੀ, ਅੱਜ ਅਸੀਂ ਗੱਲ ਕਰਾਂਗੇ।"),
}


def norm(text):
    text = unicodedata.normalize("NFC", text.lower())
    text = re.sub(r"[।॥.,!?;:\"'()\[\]{}…–—-]", " ", text)
    return text.split()


def wer(ref, hyp):
    r, h = norm(ref), norm(hyp)
    d = list(range(len(h) + 1))
    for i in range(1, len(r) + 1):
        prev, d[0] = d[0], i
        for j in range(1, len(h) + 1):
            cur = d[j]
            d[j] = min(d[j] + 1, d[j - 1] + 1, prev + (r[i - 1] != h[j - 1]))
            prev = cur
    return d[len(h)] / max(1, len(r))


def duration(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path],
                         capture_output=True, text=True)
    try:
        return float(out.stdout.strip())
    except ValueError:
        return 0.0


def transcribe(whisper, model, media, lang, tmp):
    wav = os.path.join(tmp, "a.wav")
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", media, "-vn", "-ac", "1", "-ar", "16000",
                    "-c:a", "pcm_s16le", wav], check=True)
    wl, prompt = LANG.get(lang, (lang, ""))
    base = os.path.join(tmp, "out")
    cmd = [whisper, "-m", model, "-f", wav, "-l", wl, "-ojf", "-of", base]
    if prompt:
        cmd += ["--prompt", prompt]
    t0 = time.time()
    subprocess.run(cmd, check=True, capture_output=True)
    elapsed = time.time() - t0
    data = json.load(open(base + ".json", encoding="utf-8", errors="replace"))
    text, low, total = [], 0, 0
    for seg in data.get("transcription", []):
        text.append(seg.get("text", ""))
        for tok in seg.get("tokens", []):
            if tok.get("text", "").startswith("[_"):
                continue
            total += 1
            low += 1 if tok.get("p", 1) < 0.5 else 0
    return " ".join(text), elapsed, low / max(1, total)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("samples")
    ap.add_argument("--model", required=True)
    ap.add_argument("--whisper", default=shutil.which("whisper-cli") or "whisper-cli")
    a = ap.parse_args()
    results = {}
    with tempfile.TemporaryDirectory() as tmp:
        for lang in sorted(os.listdir(a.samples)):
            ldir = os.path.join(a.samples, lang)
            if not os.path.isdir(ldir):
                continue
            for f in sorted(os.listdir(ldir)):
                stem, ext = os.path.splitext(f)
                ref = os.path.join(ldir, stem + ".txt")
                if ext.lower() not in (".wav", ".mp4", ".m4a", ".mp3", ".mov") or not os.path.exists(ref):
                    continue
                media = os.path.join(ldir, f)
                hyp, secs, lowshare = transcribe(a.whisper, a.model, media, lang, tmp)
                e = wer(open(ref, encoding="utf-8").read(), hyp)
                rtf = secs / max(0.01, duration(media))
                results.setdefault(lang, []).append((f, e, rtf, lowshare))
                print(f"{lang:9s} {f:30s} WER {e*100:5.1f}%  RTF {rtf:4.2f}  low-conf {lowshare*100:4.1f}%")
    print("\nSummary")
    for lang, rows in results.items():
        n = len(rows)
        print(f"{lang:9s} files {n:3d}  mean WER {sum(r[1] for r in rows)/n*100:5.1f}%  "
              f"mean RTF {sum(r[2] for r in rows)/n:4.2f}  low-conf {sum(r[3] for r in rows)/n*100:4.1f}%")
    if not results:
        print("No samples found. See the docstring for the expected folder layout.")
        sys.exit(1)


if __name__ == "__main__":
    main()
