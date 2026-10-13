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
The result is fully self-contained: Qt, FFmpeg + ffprobe, whisper-cli, the speech model(s), the voice-activity model and the Microsoft C++ runtime are all bundled, so the person installing it needs nothing else. Text-to-speech uses the voices built into Windows.

### Option A: let GitHub build it (no Windows PC needed)
The workflow `.github/workflows/windows.yml` runs on GitHub's Windows machines and:
1. installs Qt 6.8 (with Qt Multimedia), Ninja and Inno Setup;
2. downloads the newest stable FFmpeg GPL build and checks its SHA-256, then checks it has every filter/encoder the editor uses;
3. builds whisper-cli from whisper.cpp source (portable x64 code);
4. downloads the speech model(s) (default `small`) and checks their SHA-256 against Hugging Face;
5. builds the app, runs the tests, assembles the folder, and self-tests it (the app must start; whisper must transcribe a sample clip);
6. builds the installer, then silently installs, starts and uninstalls it as a final test.

It produces two downloads:
- **ShyftrStudio-Setup.exe**: installer with Start-menu and desktop shortcuts and an uninstaller.
- **ShyftrStudio-windows.zip**: portable; unzip it and run `Shyftr Studio.exe`.

One-time setup on the Mac:
```bash
cd ~/Projects/ShyftrStudio
bash scripts/install-workflows.sh      # makes sure .github/workflows/ has the latest workflow files
git init -b main && git add . && git commit -m "Shyftr Studio"
gh repo create shyftr-studio --private --source . --push   # or create the repo on github.com and git push
```
Each push to `main` runs the build (about 30–40 minutes the first time, faster later thanks to caching). Open the repository on GitHub → **Actions** → the latest **windows-exe** run → **Artifacts** → *ShyftrStudio-windows*. To choose which speech models to bundle, start it by hand with **Run workflow** and type e.g. `small,base` (or `none`; `large-v3` makes a ~3.5 GB installer). To publish a release, push a version tag (`git tag v0.1.0 && git push --tags`): both files are attached to a GitHub Release automatically.

### Option B: build on a Windows PC
Install once: **Visual Studio 2022** (Community is free) with "Desktop development with C++", **Git for Windows**, and **Python 3** (only used to download Qt if you don't have it). Then open **x64 Native Tools Command Prompt for VS 2022**, go to the project folder and run:
```bat
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
```
It downloads Qt (into `C:\Qt`, if not already there), FFmpeg, whisper.cpp, the speech model and Inno Setup (via winget), then builds, tests and packages everything into `dist\`. Options: `-QtDir C:\Qt\6.8.3\msvc2022_64` to use your own Qt, `-Models small,base` (or `none`), `-SkipTests`, `-NoInstaller`. Downloads are kept in `.deps\`, so re-runs are quick.

The pieces can also be run separately: `scripts\windows\fetch-deps.ps1` (downloads) and `scripts\windows\package.ps1 -QtDir ...` (assemble + self-test).

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
