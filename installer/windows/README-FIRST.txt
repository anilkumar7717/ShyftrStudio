Shyftr Studio for Windows
=========================

Run "Shyftr Studio.exe". Everything works offline; nothing is uploaded and there is no account or billing.

Everything the app needs is already in this folder - nothing else to install:
  - FFmpeg and ffprobe (ffmpeg.exe, ffprobe.exe): import, preview, effects and export
  - whisper.cpp (whisper-cli.exe): local AI captions
  - Speech model(s) in the "models" folder (ggml-small.bin by default: good for English, Hindi,
    Hinglish and Punjabi). They appear in Preferences -> AI Models as "Included with Shyftr Studio".
  - Qt and the Microsoft C++ runtime DLLs
Text-to-speech uses the voices built into Windows (Settings -> Time & language -> Speech adds more).

More or bigger speech models (optional): download e.g. ggml-medium.bin from
https://huggingface.co/ggerganov/whisper.cpp/tree/main and add it in Preferences -> AI Models -> Add model file...

Your projects are saved in Documents\ShyftrStudio\Projects.

Licenses (in the "licenses" folder): FFmpeg is GPL (https://ffmpeg.org/legal.html); whisper.cpp and the
Whisper models are MIT.
