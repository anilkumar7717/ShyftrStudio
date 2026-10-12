# Building Shyftr Studio

Requirements: CMake ≥ 3.21, a C++17 compiler, and **Qt ≥ 6.5** (Core, Gui, Widgets, Multimedia, Concurrent, Test).
At runtime the app needs `ffmpeg` and `ffprobe` (≥ 6), plus `whisper-cli` from whisper.cpp ≥ 1.7 for automatic captions.

## macOS 14+ (Apple Silicon or Intel)
```bash
brew install qt cmake ninja ffmpeg whisper-cpp
./scripts/build-mac.sh          # Release build + tests → build/Shyftr Studio.app
./scripts/build-mac.sh --bundle # also runs macdeployqt → self-contained Qt frameworks
```
Manual steps:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
ctest --test-dir build --output-on-failure
open "build/Shyftr Studio.app"
```
The app finds Homebrew's `ffmpeg`, `ffprobe` and `whisper-cli` in `/opt/homebrew/bin` automatically, even when launched from Finder. To ship the engines inside the app, place them in `Shyftr Studio.app/Contents/Resources/bin/`, which is searched first. They must be signed together with the app for notarization.

Homebrew's whisper.cpp is built with Metal, so transcription uses the Apple GPU unless *Preferences → Performance → Use GPU* is off.

## Windows 11 (x64)
```powershell
# Qt 6.8 MSVC 2022 via the Qt online installer (or aqtinstall), CMake, Ninja, Visual Studio 2022 Build Tools
.\scripts\build-windows.ps1 -QtDir C:\Qt\6.8.3\msvc2022_64
```
The script configures, builds and tests, then runs `windeployqt`. Put `ffmpeg.exe`, `ffprobe.exe` and `whisper-cli.exe` (with its DLLs) next to `Shyftr Studio.exe`, or set their paths in Preferences → Tools.

## Options
| CMake option | Default | Meaning |
|---|---|---|
| `SHYFTR_BUILD_APP` | ON | Build the desktop app (needs Widgets + Multimedia) |
| `SHYFTR_BUILD_TESTS` | ON | Build the QtTest suites |

`SHYFTR_STUDIO_ROOT=/some/folder` overrides the workspace location, which is useful for tests and portable installs.

## Speech models
```bash
./scripts/get-model.sh small      # tiny | base | small | medium | large-v3-turbo | large-v3
./scripts/get-model.sh vad        # optional Silero voice-activity model
```
Then use Preferences → AI Models → **Add model file…**, which copies the file, records its SHA-256 and provenance, and verifies it before each use. For machines with no internet access, copy the `.bin` files on a drive and add them the same way.

## App icon
The icon lives in `resources/icons/`: `ShyftrStudio.icns` (macOS bundle), `ShyftrStudio.ico` (Windows, via `resources/windows/app.rc`) and `app-icon-1024.png` (window and Dock icon at runtime). CMake wires all of them in, so a normal rebuild picks it up.
