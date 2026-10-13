# One-command Windows build of Shyftr Studio: downloads everything it needs, builds, tests and packages.
#
# Open "x64 Native Tools Command Prompt for VS 2022" (Visual Studio 2022 with "Desktop development with C++"),
# cd to the project folder and run:
#   powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
#
# What it fetches automatically (into .deps\ and C:\Qt unless told otherwise):
#   Qt 6 (via aqtinstall, only if -QtDir is not given and Qt is not found), FFmpeg + ffprobe,
#   whisper-cli (built from whisper.cpp source), the speech model(s), the C++ runtime DLLs,
#   and Inno Setup for the installer (via winget, if available).
#
# Options:
#   -QtDir C:\Qt\6.8.3\msvc2022_64   use an existing Qt instead of downloading one
#   -Models small,base               speech models to bundle (tiny/base/small/medium/large-v3-turbo/large-v3/none)
#   -SkipTests  -NoInstaller
#
# Result:
#   dist\ShyftrStudio\Shyftr Studio.exe   portable app folder
#   dist\ShyftrStudio-windows.zip          the same, zipped to share
#   dist\ShyftrStudio-Setup.exe            installer (Start menu + desktop shortcut + uninstaller)
param(
    [string]$QtDir = "",
    [string]$QtVersion = "6.8.3",
    [string[]]$Models = @("small"),
    [string]$Config = "Release",
    [switch]$SkipTests,
    [switch]$NoInstaller
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
Set-Location (Join-Path $PSScriptRoot "..")
function Step($msg) { Write-Host ""; Write-Host "==> $msg" -ForegroundColor Cyan }

# --------------------------------------------------------------------------------- build tools
Step "Checking build tools"
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw "cl.exe not found. Install Visual Studio 2022 (Community is free) with 'Desktop development with C++', " +
          "then run this from 'x64 Native Tools Command Prompt for VS 2022'."
}
foreach ($t in "cmake", "git") {
    if (-not (Get-Command $t -ErrorAction SilentlyContinue)) { throw "$t not found (it comes with Visual Studio 2022 / Git for Windows)." }
}
$generator = if (Get-Command ninja -ErrorAction SilentlyContinue) { "Ninja" } else { "NMake Makefiles" }
Write-Host "  compiler: cl.exe, generator: $generator"

# -------------------------------------------------------------------------------------------- Qt
Step "Qt $QtVersion"
if (-not $QtDir) {
    foreach ($c in "C:\Qt\$QtVersion\msvc2022_64", "$env:USERPROFILE\Qt\$QtVersion\msvc2022_64") {
        if (Test-Path "$c\bin\windeployqt.exe") { $QtDir = $c; break }
    }
}
if (-not $QtDir) {
    $py = Get-Command py, python -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $py) { throw "Qt not found and Python is not installed to download it. Install Qt $QtVersion (msvc2022_64 + Qt Multimedia) and pass -QtDir, or install Python 3." }
    Write-Host "  downloading Qt $QtVersion with aqtinstall (one time, ~1 GB) ..."
    & $py.Source -m pip install --quiet --upgrade aqtinstall
    if ($LASTEXITCODE) { throw "pip install aqtinstall failed" }
    & $py.Source -m aqt install-qt windows desktop $QtVersion win64_msvc2022_64 -m qtmultimedia -O C:\Qt
    if ($LASTEXITCODE) { throw "Qt download failed" }
    $QtDir = "C:\Qt\$QtVersion\msvc2022_64"
}
if (-not (Test-Path "$QtDir\bin\windeployqt.exe")) { throw "Qt not found at $QtDir (expected bin\windeployqt.exe)" }
Write-Host "  $QtDir"

# ------------------------------------------------------------- FFmpeg, whisper-cli, models, runtime
& (Join-Path $PSScriptRoot "windows\fetch-deps.ps1") -Models $Models
if (-not $?) { throw "Fetching dependencies failed" }

# ---------------------------------------------------------------------------------------- build
Step "Building Shyftr Studio ($Config)"
cmake -S . -B build -G $generator -DCMAKE_BUILD_TYPE=$Config "-DCMAKE_PREFIX_PATH=$QtDir"
if ($LASTEXITCODE) { throw "CMake configure failed" }
cmake --build build --config $Config
if ($LASTEXITCODE) { throw "Build failed" }

if (-not $SkipTests) {
    Step "Running tests"
    $env:PATH = "$QtDir\bin;$((Resolve-Path .deps\ffmpeg).Path);$((Resolve-Path .deps\whisper).Path);$env:PATH"
    $env:QT_QPA_PLATFORM = "offscreen"
    ctest --test-dir build --output-on-failure
    $testsFailed = $LASTEXITCODE -ne 0
    Remove-Item Env:QT_QPA_PLATFORM
    if ($testsFailed) { Write-Host "Some tests failed (see above). Packaging anyway." -ForegroundColor Yellow }
}

# -------------------------------------------------------------------------------------- package
if (Test-Path dist) { Remove-Item -Recurse -Force dist }
& (Join-Path $PSScriptRoot "windows\package.ps1") -QtDir $QtDir
if (-not $?) { throw "Packaging failed" }

# ------------------------------------------------------------------------------------ installer
if (-not $NoInstaller) {
    Step "Installer"
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
              "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc -and (Get-Command winget -ErrorAction SilentlyContinue)) {
        Write-Host "  installing Inno Setup with winget ..."
        winget install --id JRSoftware.InnoSetup -e --silent --accept-package-agreements --accept-source-agreements | Out-Null
        $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") |
            Where-Object { Test-Path $_ } | Select-Object -First 1
    }
    if ($iscc) {
        & $iscc /Q installer\windows\ShyftrStudio.iss
        if ($LASTEXITCODE) { throw "Installer build failed" }
        Write-Host "  dist\ShyftrStudio-Setup.exe"
    } else {
        Write-Host "  Inno Setup not available - skipped (install it from https://jrsoftware.org/isdl.php to get the installer)." -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "Done." -ForegroundColor Green
Write-Host "  App:       dist\ShyftrStudio\Shyftr Studio.exe"
Write-Host "  Zip:       dist\ShyftrStudio-windows.zip"
if (Test-Path dist\ShyftrStudio-Setup.exe) { Write-Host "  Installer: dist\ShyftrStudio-Setup.exe" }
