# Assembles the self-contained Windows app folder and checks it really runs:
#   dist\ShyftrStudio\  Shyftr Studio.exe + Qt + FFmpeg + whisper-cli + speech models + C++ runtime
#   dist\ShyftrStudio-windows.zip
# Run after building (cmake --build build) and scripts\windows\fetch-deps.ps1:
#   powershell -ExecutionPolicy Bypass -File scripts\windows\package.ps1 -QtDir C:\Qt\6.8.3\msvc2022_64
param(
    [Parameter(Mandatory = $true)][string]$QtDir,
    [string]$BuildDir = "build",
    [string]$DepsDir = ".deps",
    [string]$Dist = "dist\ShyftrStudio",
    [switch]$NoZip
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..\..")
function Step($msg) { Write-Host ""; Write-Host "==> $msg" -ForegroundColor Cyan }

$exeName = "Shyftr Studio.exe"
if (-not (Test-Path "$BuildDir\$exeName")) { throw "$BuildDir\$exeName not found - build the app first." }
if (-not (Test-Path "$QtDir\bin\windeployqt.exe")) { throw "Qt not found at $QtDir (expected bin\windeployqt.exe)" }
$Deps = (Resolve-Path $DepsDir).Path

Step "Assembling $Dist"
if (Test-Path $Dist) { Remove-Item -Recurse -Force $Dist }
New-Item -ItemType Directory -Force $Dist | Out-Null
Copy-Item "$BuildDir\$exeName" $Dist

# Qt DLLs and plugins (platform, styles, image formats, multimedia + its FFmpeg DLLs)
& "$QtDir\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "$Dist\$exeName"
if ($LASTEXITCODE) { throw "windeployqt failed" }
# Qt Multimedia's FFmpeg backend needs Qt's own FFmpeg DLLs; older windeployqt versions miss them.
if ((Test-Path "$Dist\multimedia") -and -not (Get-ChildItem $Dist -Filter "avcodec*.dll")) {
    Get-ChildItem "$QtDir\bin" -Include "avcodec*.dll", "avformat*.dll", "avutil*.dll", "swresample*.dll", "swscale*.dll" -Recurse |
        Copy-Item -Destination $Dist
}

# Microsoft C++ runtime, app-local (works on PCs that never installed the VC++ redistributable)
Copy-Item "$Deps\vcredist\*.dll" $Dist -Force
Remove-Item "$Dist\vc_redist*.exe" -ErrorAction SilentlyContinue

# Local engines: FFmpeg and whisper.cpp
Copy-Item "$Deps\ffmpeg\ffmpeg.exe", "$Deps\ffmpeg\ffprobe.exe" $Dist
Copy-Item "$Deps\whisper\*" $Dist -Exclude "VERSION.txt"
New-Item -ItemType Directory -Force "$Dist\licenses" | Out-Null
Move-Item "$Dist\WHISPER-LICENSE.txt" "$Dist\licenses\" -Force -ErrorAction SilentlyContinue
if (Test-Path "$Deps\ffmpeg\FFMPEG-LICENSE.txt") { Copy-Item "$Deps\ffmpeg\FFMPEG-LICENSE.txt" "$Dist\licenses\" }
if (Test-Path "LICENSE") { Copy-Item "LICENSE" "$Dist\licenses\ShyftrStudio-LICENSE.txt" }

# Speech models (found by the app in <app>\models, read-only, listed as "Included with Shyftr Studio")
$models = @(Get-ChildItem "$Deps\models" -Filter "*.bin" -ErrorAction SilentlyContinue)
if ($models) {
    New-Item -ItemType Directory -Force "$Dist\models" | Out-Null
    Copy-Item "$Deps\models\*.bin", "$Deps\models\*.sha256" "$Dist\models\"
}
Copy-Item "installer\windows\README-FIRST.txt" $Dist

Step "Checking the package"
$required = @($exeName, "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Multimedia.dll",
              "platforms\qwindows.dll", "ffmpeg.exe", "ffprobe.exe", "whisper-cli.exe",
              "vcruntime140.dll", "msvcp140.dll")
$missing = $required | Where-Object { -not (Test-Path (Join-Path $Dist $_)) }
if ($missing) { throw "Missing from the package: $($missing -join ', ')" }
# Only the files the exe itself loads are deployed (e.g. no Qt6Concurrent.dll: the app uses its
# header-only parts). Missing DLLs are caught for real by the start-up test below.
if (-not (Test-Path "$Dist\imageformats\qjpeg.dll")) {
    Write-Host "  warning: no JPEG image plugin - JPEG photos may not import" -ForegroundColor Yellow
}
if (-not (Get-ChildItem "$Dist\multimedia" -Filter *.dll -ErrorAction SilentlyContinue)) {
    Write-Host "  warning: no Qt Multimedia backend plugin - video preview may not play" -ForegroundColor Yellow
}
& "$Dist\ffmpeg.exe" -hide_banner -version | Select-Object -First 1
& "$Dist\ffprobe.exe" -hide_banner -version | Select-Object -First 1

# whisper-cli must start (a missing DLL shows up here) ...
$help = & "$Dist\whisper-cli.exe" --help 2>&1 | Out-String
if ($help -notmatch "usage") { throw "whisper-cli.exe did not start:`n$help" }
# ... and, if a model is bundled, really transcribe the whisper.cpp sample
$speech = $models | Where-Object { $_.Name -notlike "*silero*" } | Sort-Object Length | Select-Object -First 1
if ($speech -and (Test-Path "$Deps\jfk.wav")) {
    $txt = & "$Dist\whisper-cli.exe" -m "$Dist\models\$($speech.Name)" -f "$Deps\jfk.wav" -l en -nt -np 2>$null | Out-String
    if ($txt -notmatch "(?i)country") { throw "Transcription self-test failed with $($speech.Name):`n$txt" }
    Write-Host "  transcription self-test ok ($($speech.Name)): $($txt.Trim())"
}

# The app itself must start with only what is in the folder (catches missing DLLs/plugins).
$env:QT_QPA_PLATFORM = "offscreen"
$env:SHYFTR_STUDIO_ROOT = Join-Path $env:TEMP "shyftr-smoke-$PID"
$p = Start-Process -FilePath (Resolve-Path "$Dist\$exeName").Path -PassThru -WorkingDirectory $env:TEMP
Start-Sleep -Seconds 12
if ($p.HasExited) { throw "Shyftr Studio.exe exited during start-up (exit code $($p.ExitCode)). A DLL or plugin is probably missing." }
Stop-Process -Id $p.Id -Force
Remove-Item Env:QT_QPA_PLATFORM, Env:SHYFTR_STUDIO_ROOT
Write-Host "  app start-up ok"

$size = (Get-ChildItem $Dist -Recurse | Measure-Object Length -Sum).Sum
Write-Host ("  package size: {0:N0} MB, models: {1}" -f ($size / 1MB), $(if ($models) { ($models.Name -join ', ') } else { 'none' }))

if (-not $NoZip) {
    Step "Zipping"
    $zip = "dist\ShyftrStudio-windows.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    # .NET zip (Compress-Archive fails on files over 2 GB and is slow)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory((Resolve-Path $Dist).Path, (Join-Path (Resolve-Path "dist").Path "ShyftrStudio-windows.zip"),
        [System.IO.Compression.CompressionLevel]::Optimal, $true)
    Write-Host "  $zip"
}
