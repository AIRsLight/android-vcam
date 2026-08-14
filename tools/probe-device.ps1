param(
    [string]$Adb = "D:\AndroidSdk\platform-tools\adb.exe",
    [string]$Output = "out/device-inspection/device-profile.conf"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$probe = Join-Path $root "apmodule/device-probe.sh"
$outputPath = Join-Path $root $Output
$remoteProbe = "/data/local/tmp/android-vcam-device-probe.sh"

if (-not (Test-Path -LiteralPath $probe)) {
    throw "Device probe not found: $probe"
}

& $Adb get-state | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "ADB device is not connected"
}

& $Adb push $probe $remoteProbe | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Unable to upload device probe"
}

try {
    & $Adb shell chmod 0755 $remoteProbe
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to make device probe executable"
    }

    $profile = & $Adb shell $remoteProbe
    if ($LASTEXITCODE -ne 0) {
        throw "Device capability probe failed"
    }

    $outputDirectory = Split-Path -Parent $outputPath
    New-Item -ItemType Directory -Force $outputDirectory | Out-Null
    Set-Content -LiteralPath $outputPath -Value $profile -Encoding utf8NoBOM
    $profile
    Write-Output "Profile written to $outputPath"
} finally {
    & $Adb shell rm -f $remoteProbe 2>$null
}
