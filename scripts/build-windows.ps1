# Build, test and deploy Shyftr Studio on Windows 11 x64.
#   .\scripts\build-windows.ps1 -QtDir C:\Qt\6.8.3\msvc2022_64
param([Parameter(Mandatory=$true)][string]$QtDir, [string]$Config = "Release")
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
# Requires "x64 Native Tools Command Prompt for VS 2022" environment (cl.exe on PATH)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=$Config -DCMAKE_PREFIX_PATH="$QtDir"
cmake --build build
$env:QT_QPA_PLATFORM = "offscreen"
$env:PATH = "$QtDir\bin;$env:PATH"
ctest --test-dir build --output-on-failure
& "$QtDir\bin\windeployqt.exe" --release --no-translations "build\Shyftr Studio.exe"
Write-Host "Built build\Shyftr Studio.exe"
Write-Host "Place ffmpeg.exe, ffprobe.exe and whisper-cli.exe (+ DLLs) next to it, or set paths in Preferences -> Tools."
