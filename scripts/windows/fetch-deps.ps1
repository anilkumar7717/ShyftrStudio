# Downloads / builds everything Shyftr Studio needs at run time on Windows, into .deps\ :
#   .deps\ffmpeg\     ffmpeg.exe, ffprobe.exe      (BtbN GPL static build: x264, x265, lame, vidstab, ...)
#   .deps\whisper\    whisper-cli.exe (+ DLLs)     (built from whisper.cpp source, portable CPU code)
#   .deps\models\     ggml-*.bin speech models     (+ .sha256 files, checked against Hugging Face)
#   .deps\vcredist\   Microsoft C++ runtime DLLs   (so the app runs on PCs without the VC++ redistributable)
# Used by scripts\build-windows.ps1 and the GitHub workflow. Safe to re-run: finished parts are skipped.
#
#   powershell -ExecutionPolicy Bypass -File scripts\windows\fetch-deps.ps1 [-Models small,base] [-DepsDir .deps]
#
# -Models: any of tiny, base, small, medium, large-v3-turbo, large-v3 (comma separated), or none.
#          "small" is the recommended default for Hindi / Hinglish / Punjabi (~466 MB).
# Needs: PowerShell 5+, git, and for whisper-cli: cmake + the MSVC compiler (run from the
# "x64 Native Tools Command Prompt for VS 2022", or with cl.exe on PATH).
param(
    [string[]]$Models = @("small"),
    [string]$DepsDir = ".deps",
    [string]$WhisperVersion = "v1.9.5",
    [switch]$SkipWhisperBuild,
    [switch]$Vad = $true
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"   # Invoke-WebRequest is many times faster without the progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Set-Location (Join-Path $PSScriptRoot "..\..")
New-Item -ItemType Directory -Force $DepsDir | Out-Null
$Deps = (Resolve-Path $DepsDir).Path
$Models = @($Models | ForEach-Object { $_ -split "," } | ForEach-Object { $_.Trim().ToLower() } | Where-Object { $_ -and $_ -ne "none" })

function Step($msg) { Write-Host ""; Write-Host "==> $msg" -ForegroundColor Cyan }
function Download($url, $out) {
    for ($i = 1; $i -le 3; $i++) {
        try {
            Invoke-WebRequest -Uri $url -OutFile $out -UseBasicParsing -Headers @{ "User-Agent" = "ShyftrStudio-build" }
            return
        } catch {
            if ($i -eq 3) { throw "Download failed: $url`n$_" }
            Write-Host "  retry $i ($($_.Exception.Message))"; Start-Sleep -Seconds (5 * $i)
        }
    }
}
function Sha256($path) { (Get-FileHash -Algorithm SHA256 $path).Hash.ToLower() }

# ------------------------------------------------------------------------------------------ FFmpeg
Step "FFmpeg"
$ffDir = Join-Path $Deps "ffmpeg"
if ((Test-Path "$ffDir\ffmpeg.exe") -and (Test-Path "$ffDir\ffprobe.exe")) {
    Write-Host "  already present: $ffDir"
} else {
    $base = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest"
    $sums = Join-Path $Deps "ffmpeg-checksums.sha256"
    Download "$base/checksums.sha256" $sums
    $lines = Get-Content $sums
    # newest stable release branch (ffmpeg-nX.Y-latest-win64-gpl-X.Y.zip), else the master build
    $stable = $lines | ForEach-Object {
        if ($_ -match '^([0-9a-f]{64})\s+(ffmpeg-n(\d+)\.(\d+)-latest-win64-gpl-\d+\.\d+\.zip)$') {
            [pscustomobject]@{ Hash = $matches[1]; File = $matches[2]; Ver = [version]"$($matches[3]).$($matches[4])" }
        }
    } | Sort-Object Ver -Descending | Select-Object -First 1
    if (-not $stable) {
        $m = $lines | Where-Object { $_ -match '^([0-9a-f]{64})\s+(ffmpeg-master-latest-win64-gpl\.zip)$' } | Select-Object -First 1
        if (-not $m) { throw "Could not find a Windows FFmpeg build in $base/checksums.sha256" }
        $null = $m -match '^([0-9a-f]{64})\s+(\S+)$'
        $stable = [pscustomobject]@{ Hash = $matches[1]; File = $matches[2] }
    }
    Write-Host "  $($stable.File)"
    $zip = Join-Path $Deps $stable.File
    Download "$base/$($stable.File)" $zip
    if ((Sha256 $zip) -ne $stable.Hash) { Remove-Item $zip; throw "FFmpeg download is corrupted (SHA-256 mismatch)" }
    $tmp = Join-Path $Deps "ffmpeg-extract"
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    Expand-Archive $zip -DestinationPath $tmp
    $bin = (Get-ChildItem $tmp -Recurse -Filter ffmpeg.exe | Select-Object -First 1).DirectoryName
    New-Item -ItemType Directory -Force $ffDir | Out-Null
    Copy-Item "$bin\ffmpeg.exe", "$bin\ffprobe.exe" $ffDir
    $lic = Get-ChildItem $tmp -Recurse -Filter LICENSE* | Select-Object -First 1
    if ($lic) { Copy-Item $lic.FullName "$ffDir\FFMPEG-LICENSE.txt" }
    Remove-Item -Recurse -Force $tmp, $zip
}
# Check the filters and encoders the editor relies on are really in this build.
$filters = & "$ffDir\ffmpeg.exe" -hide_banner -filters 2>&1 | Out-String
$encoders = & "$ffDir\ffmpeg.exe" -hide_banner -encoders 2>&1 | Out-String
$missing = @()
foreach ($f in "silencedetect", "loudnorm", "afftdn", "sidechaincompress", "afftfilt", "aecho", "palettegen", "paletteuse",
               "vidstabdetect", "vidstabtransform", "deshake", "select", "showinfo", "atempo", "amix") {
    if ($filters -notmatch "\s$f\s") { $missing += "filter $f" }
}
foreach ($e in "libx264", "libx265", "aac", "libmp3lame", "flac", "gif", "png", "pcm_s16le") {
    if ($encoders -notmatch "\s$e\s") { $missing += "encoder $e" }
}
$optional = @("filter vidstabdetect", "filter vidstabtransform", "encoder libx265")   # app has fallbacks for these
$fatal = $missing | Where-Object { $optional -notcontains $_ }
if ($missing) { Write-Host "  not in this FFmpeg build: $($missing -join ', ')" -ForegroundColor Yellow }
if ($fatal) { throw "FFmpeg build lacks required features: $($fatal -join ', ')" }
& "$ffDir\ffmpeg.exe" -hide_banner -version | Select-Object -First 1

# ------------------------------------------------------------------------------------- whisper-cli
Step "whisper.cpp $WhisperVersion (whisper-cli)"
$wDir = Join-Path $Deps "whisper"
$stamp = Join-Path $wDir "VERSION.txt"
if ((Test-Path "$wDir\whisper-cli.exe") -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $WhisperVersion)) {
    Write-Host "  already present: $wDir"
} elseif ($SkipWhisperBuild) {
    Write-Host "  skipped (-SkipWhisperBuild)"
} else {
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "cl.exe not found. Run from 'x64 Native Tools Command Prompt for VS 2022' (needed to build whisper-cli)."
    }
    $src = Join-Path $Deps "whisper-src"
    $bld = Join-Path $Deps "whisper-build"
    if (Test-Path $src) { Remove-Item -Recurse -Force $src }
    if (Test-Path $bld) { Remove-Item -Recurse -Force $bld }
    git clone --quiet --depth 1 --branch $WhisperVersion https://github.com/ggml-org/whisper.cpp $src
    if ($LASTEXITCODE) { throw "git clone of whisper.cpp failed" }
    $gen = if (Get-Command ninja -ErrorAction SilentlyContinue) { "Ninja" } else { "NMake Makefiles" }
    # GGML_NATIVE=OFF: portable x64 code (runs on any PC, not just the build machine).
    # Static libs: one self-contained whisper-cli.exe.
    cmake -S $src -B $bld -G $gen -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DGGML_NATIVE=OFF `
          -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_SERVER=OFF -DWHISPER_BUILD_EXAMPLES=ON
    if ($LASTEXITCODE) { throw "whisper.cpp configure failed" }
    cmake --build $bld --config Release --target whisper-cli
    if ($LASTEXITCODE) { throw "whisper.cpp build failed" }
    New-Item -ItemType Directory -Force $wDir | Out-Null
    $exe = Get-ChildItem $bld -Recurse -Filter whisper-cli.exe | Select-Object -First 1
    Copy-Item $exe.FullName $wDir
    Get-ChildItem $exe.DirectoryName -Filter *.dll | Copy-Item -Destination $wDir
    Copy-Item "$src\LICENSE" "$wDir\WHISPER-LICENSE.txt"
    Copy-Item "$src\samples\jfk.wav" (Join-Path $Deps "jfk.wav")   # used by the package self-test
    Set-Content $stamp $WhisperVersion
    Remove-Item -Recurse -Force $bld
}

# ------------------------------------------------------------------------------------ speech models
Step "Speech models: $(if ($Models) { $Models -join ', ' } else { 'none' })"
$mDir = Join-Path $Deps "models"
New-Item -ItemType Directory -Force $mDir | Out-Null
$known = @("tiny", "base", "small", "medium", "large-v3-turbo", "large-v3", "tiny.en", "base.en", "small.en", "medium.en")
$hfTree = $null
function HfHashes($repo) {
    try {
        $t = Invoke-RestMethod -Uri "https://huggingface.co/api/models/$repo/tree/main" -UseBasicParsing
        $h = @{}; foreach ($e in $t) { if ($e.lfs) { $h[$e.path] = @{ sha = $e.lfs.oid; size = [int64]$e.lfs.size } } }
        return $h
    } catch { Write-Host "  (could not read checksums from Hugging Face: $($_.Exception.Message))" -ForegroundColor Yellow; return @{} }
}
function GetModel($repo, $file, $minBytes, $hashes) {
    $out = Join-Path $mDir $file
    $want = $hashes[$file]
    if (Test-Path $out) {
        if ((-not $want) -or ((Get-Item $out).Length -eq $want.size -and (Test-Path "$out.sha256"))) {
            Write-Host "  already present: $file"; return
        }
    }
    Write-Host "  downloading $file ..."
    $part = "$out.part"
    Download "https://huggingface.co/$repo/resolve/main/$file" $part
    $len = (Get-Item $part).Length
    $fs = [System.IO.File]::OpenRead($part); $buf = New-Object byte[] 4; [void]$fs.Read($buf, 0, 4); $fs.Close()
    $magic = [System.Text.Encoding]::ASCII.GetString($buf)
    if (($magic -ne "lmgg" -and $magic -ne "GGUF") -or $len -lt $minBytes) {
        Remove-Item $part; throw "$file is not a valid whisper.cpp model ($len bytes, header '$magic')"
    }
    $sha = Sha256 $part
    if ($want -and $sha -ne $want.sha) { Remove-Item $part; throw "$file SHA-256 mismatch (got $sha, expected $($want.sha))" }
    Move-Item -Force $part $out
    Set-Content -NoNewline "$out.sha256" "$sha  $file"
    Write-Host "  ok  $file  $([math]::Round($len / 1MB)) MB  sha256 $sha"
}
if ($Models) {
    $h = HfHashes "ggerganov/whisper.cpp"
    foreach ($m in $Models) {
        if ($known -notcontains $m) { throw "Unknown model '$m'. Choose from: $($known -join ', ')" }
        GetModel "ggerganov/whisper.cpp" "ggml-$m.bin" (20MB) $h
    }
}
if ($Vad) {
    # Optional voice-activity-detection model (tiny); improves timing on long silences.
    try { GetModel "ggml-org/whisper-vad" "ggml-silero-v5.1.2.bin" (100KB) (HfHashes "ggml-org/whisper-vad") }
    catch { Write-Host "  VAD model skipped: $_" -ForegroundColor Yellow }
}
# remove models that are no longer requested, so the package only carries what was asked for
Get-ChildItem $mDir -Filter "ggml-*.bin" | Where-Object {
    $n = $_.BaseName -replace '^ggml-', ''
    ($n -notlike "silero*") -and ($Models -notcontains $n)
} | ForEach-Object { Write-Host "  removing unrequested $($_.Name)"; Remove-Item $_.FullName, "$($_.FullName).sha256" -ErrorAction SilentlyContinue }

# ----------------------------------------------------------------------------- C++ runtime DLLs
Step "Microsoft C++ runtime"
$vc = Join-Path $Deps "vcredist"
New-Item -ItemType Directory -Force $vc | Out-Null
$redist = $env:VCToolsRedistDir
$copied = $false
if ($redist -and (Test-Path "$redist\x64")) {
    Get-ChildItem "$redist\x64" -Directory | Where-Object { $_.Name -match '^Microsoft\.VC\d+\.(CRT|OpenMP)$' } |
        ForEach-Object { Copy-Item "$($_.FullName)\*.dll" $vc -Force; $copied = $true }
}
if (-not $copied) {
    foreach ($d in "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
                   "msvcp140_atomic_wait.dll", "concrt140.dll", "vcomp140.dll") {
        $p = Join-Path $env:WINDIR "System32\$d"
        if (Test-Path $p) { Copy-Item $p $vc -Force; $copied = $true }
    }
}
if (-not $copied) { throw "Could not find the Microsoft C++ runtime DLLs (install Visual Studio 2022 C++ tools)." }
Get-ChildItem $vc -Filter *.dll | ForEach-Object { Write-Host "  $($_.Name)" }

Step "Dependencies ready in $Deps"
