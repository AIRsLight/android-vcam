[CmdletBinding()]
param(
    [string]$AndroidSdk = "D:\AndroidSdk",
    [string]$Serial = "",
    [string]$FfmpegTag = "n4.2.2"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$referenceRoot = Join-Path $repoRoot ".reference"
$sourceRoot = Join-Path $referenceRoot "ffmpeg"
$libraryRoot = Join-Path $referenceRoot "device-ffmpeg\lib64"
$adb = Join-Path $AndroidSdk "platform-tools\adb.exe"

if (-not (Test-Path -LiteralPath $adb)) { throw "adb not found: $adb" }
if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot ".git"))) {
    if (Test-Path -LiteralPath $sourceRoot) {
        throw "FFmpeg reference path exists but is not a Git checkout: $sourceRoot"
    }
    & git clone --depth 1 --branch $FfmpegTag `
        https://git.ffmpeg.org/ffmpeg.git $sourceRoot
    if ($LASTEXITCODE -ne 0) { throw "Unable to clone FFmpeg $FfmpegTag" }
}

New-Item -ItemType Directory -Force $libraryRoot | Out-Null
$adbArgs = @()
if ($Serial) { $adbArgs += @("-s", $Serial) }
foreach ($name in @("libavcodec.so", "libavformat.so", "libavutil.so", "libswscale.so")) {
    $destination = Join-Path $libraryRoot $name
    & $adb @adbArgs pull "/system/system_ext/lib64/$name" $destination
    if ($LASTEXITCODE -ne 0) { throw "Unable to pull $name from the target" }
}

Write-Host "FFmpeg $FfmpegTag headers: $sourceRoot"
Write-Host "Pinned device FFmpeg libraries: $libraryRoot"
