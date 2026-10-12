# Build, test and package Shyftr Studio on Windows 10/11 x64.
# Run from "x64 Native Tools Command Prompt for VS 2022" (or a PowerShell with cl.exe on PATH):
#   powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -QtDir C:\Qt\6.8.3\msvc2022_64
# Result: dist\ShyftrStudio\Shyftr Studio.exe (+ Qt DLLs), dist\ShyftrStudio-windows.zip and,
# if Inno Setup is installed, the installer dist\ShyftrStudio-Setup.exe
param([Parameter(Mandatory=$true)][string]$QtDir, [string]$Config = "Release", [switch]$SkipTests)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw "cl.exe not found. Open 'x64 Native Tools Command Prompt for VS 2022' and run this script from there."
}
if (-not (Test-Path "$QtDir\bin\windeployqt.exe")) { throw "Qt not found at $QtDir (expected bin\windeployqt.exe)" }

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=$Config -DCMAKE_PREFIX_PATH="$QtDir"
if ($LASTEXITCODE) { throw "CMake configure failed" }
cmake --build build
if ($LASTEXITCODE) { throw "Build failed" }

$env:PATH = "$QtDir\bin;$env:PATH"
if (-not $SkipTests) {
    $env:QT_QPA_PLATFORM = "offscreen"
    ctest --test-dir build --output-on-failure
    Remove-Item Env:QT_QPA_PLATFORM
}

# Self-contained folder: the exe, Qt DLLs/plugins, and the local engines if found on PATH.
$dist = "dist\ShyftrStudio"
if (Test-Path dist) { Remove-Item -Recurse -Force dist }
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item "build\Shyftr Studio.exe" $dist
& "$QtDir\bin\windeployqt.exe" --release --no-translations --compiler-runtime "$dist\Shyftr Studio.exe"
if ($LASTEXITCODE) { throw "windeployqt failed" }
foreach ($tool in "ffmpeg.exe", "ffprobe.exe", "whisper-cli.exe") {
    $found = Get-Command $tool -ErrorAction SilentlyContinue
    if ($found) {
        Copy-Item $found.Source $dist
        # whisper.cpp ships DLLs next to whisper-cli.exe
        if ($tool -eq "whisper-cli.exe") { Copy-Item (Join-Path (Split-Path $found.Source) "*.dll") $dist -ErrorAction SilentlyContinue }
        Write-Host "Bundled $tool"
    } else {
        Write-Host "Note: $tool not on PATH - copy it into $dist later, or set its path in Preferences -> Tools."
    }
}
Copy-Item installer\windows\README-FIRST.txt $dist
Compress-Archive -Path $dist -DestinationPath "dist\ShyftrStudio-windows.zip" -Force
# Optional installer (needs Inno Setup 6: winget install JRSoftware.InnoSetup)
$iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $iscc)) { $iscc = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" }
if (Test-Path $iscc) {
    & $iscc installer\windows\ShyftrStudio.iss
    if ($LASTEXITCODE) { throw "Installer build failed" }
    Write-Host "Installer: dist\ShyftrStudio-Setup.exe"
} else {
    Write-Host "Inno Setup not found - skipped the installer (winget install JRSoftware.InnoSetup to enable it)."
}
Write-Host ""
Write-Host "Done: $dist\Shyftr Studio.exe"
Write-Host "Zip to share: dist\ShyftrStudio-windows.zip"
