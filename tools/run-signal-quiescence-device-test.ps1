[CmdletBinding()]
param(
    [string]$AndroidSdk = "D:\AndroidSdk",
    [string]$BuildDirectory = "out\runtime-arm64-static"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$adb = Join-Path $AndroidSdk "platform-tools\adb.exe"
$localBinary = Join-Path $repoRoot "$BuildDirectory\android_signal_quiescence_backend_test"
$remoteBinary = "/data/local/tmp/vcam-signal-quiescence-test"

foreach ($required in @($adb, $localBinary)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required test input is missing: $required"
    }
}

$devices = & $adb devices
if ($LASTEXITCODE -ne 0) {
    throw "adb devices failed"
}
$connected = @($devices | Select-Object -Skip 1 | Where-Object { $_ -match "\tdevice$" })
if ($connected.Count -ne 1) {
    throw "Exactly one authorized Android device is required; found $($connected.Count)"
}

try {
    & $adb push $localBinary $remoteBinary
    if ($LASTEXITCODE -ne 0) { throw "Unable to push the signal-quiescence test" }
    & $adb shell chmod 0755 $remoteBinary
    if ($LASTEXITCODE -ne 0) { throw "Unable to mark the device test executable" }
    & $adb shell $remoteBinary
    if ($LASTEXITCODE -ne 0) {
        throw "Signal-quiescence device test failed with exit code $LASTEXITCODE"
    }
    Write-Output "Android ARM64 signal-quiescence test passed"
}
finally {
    & $adb shell rm -f $remoteBinary 2>$null
}
