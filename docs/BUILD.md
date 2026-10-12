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

## Windows 10/11 (x64): make the .exe
Build on a Windows PC (a Mac can't produce the Windows .exe). Install once: Visual Studio 2022 Build Tools ("Desktop development with C++"), Qt 6.8 for MSVC 2022 64-bit with the **Qt Multimedia** module, CMake and Ninja (`winget install Kitware.CMake Ninja-build.Ninja`), and FFmpeg (`winget install Gyan.FFmpeg`).

Open **x64 Native Tools Command Prompt for VS 2022**, go to the project folder and run:
```bat
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -QtDir C:\Qt\6.8.3\msvc2022_64
```
This builds, runs the tests (add `-SkipTests` to skip them) and creates `dist\ShyftrStudio\Shyftr Studio.exe` with every DLL it needs, plus `dist\ShyftrStudio-windows.zip` to share. FFmpeg, ffprobe and whisper-cli are copied in when they're on PATH; otherwise put them next to the .exe or set their paths in Preferences → Tools.

### No Windows PC? Let GitHub build it
The workflow `windows.yml` builds everything on GitHub's Windows machines. It bundles Qt, FFmpeg and a whisper-cli built from source, and produces two downloads:
- **ShyftrStudio-windows.zip**: portable; unzip it and run `Shyftr Studio.exe`.
- **ShyftrStudio-Setup.exe**: an installer with Start-menu and desktop shortcuts and an uninstaller.

One-time setup on the Mac:
```bash
cd ~/Projects/ShyftrStudio
bash scripts/install-workflows.sh      # copies ci/workflows/*.yml into .github/workflows/
git init -b main && git add . && git commit -m "Shyftr Studio"
gh repo create shyftr-studio --private --source . --push   # or create the repo on github.com and git push
```
Each push to `main` runs the build (about 20–30 minutes the first time). Open the repository on GitHub → **Actions** → the latest **windows-exe** run → **Artifacts** → *ShyftrStudio-windows*. You can also start it by hand there with **Run workflow**. To publish a release, push a version tag (`git tag v0.1.0 && git push --tags`): both files are attached to a GitHub Release automatically.

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
