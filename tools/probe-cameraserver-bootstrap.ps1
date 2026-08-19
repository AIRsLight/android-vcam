[CmdletBinding()]
param(
    [string]$AndroidSdk = "D:\AndroidSdk",
    [string]$Serial = "",
    [string]$Output = "out\device-inspection\cameraserver-bootstrap.conf"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$adb = Join-Path $AndroidSdk "platform-tools\adb.exe"
$probe = Join-Path $PSScriptRoot "device-cameraserver-bootstrap-probe.sh"
$outputPath = Join-Path $repoRoot $Output
$remoteProbe = "/data/local/tmp/vcam-cameraserver-bootstrap-probe.sh"
$adbTarget = @()
if ($Serial) { $adbTarget = @("-s", $Serial) }

foreach ($required in @($adb, $probe)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required probe input is missing: $required"
    }
}

& $adb @adbTarget get-state | Out-Null
if ($LASTEXITCODE -ne 0) { throw "ADB device is not connected" }

& $adb @adbTarget push $probe $remoteProbe | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Unable to upload bootstrap probe" }

try {
    & $adb @adbTarget shell chmod 0755 $remoteProbe
    if ($LASTEXITCODE -ne 0) { throw "Unable to make bootstrap probe executable" }

    $rootIdentity = & $adb @adbTarget shell su -c id 2>$null
    if ($LASTEXITCODE -ne 0 -or ($rootIdentity -join " ") -notmatch "uid=0") {
        throw "Bootstrap probe requires an authorized su shell"
    }

    $profile = & $adb @adbTarget shell su -c $remoteProbe
    if ($LASTEXITCODE -ne 0) { throw "Bootstrap capability probe failed" }

    New-Item -ItemType Directory -Force (Split-Path -Parent $outputPath) | Out-Null
    Set-Content -LiteralPath $outputPath -Value $profile -Encoding utf8NoBOM
    $profile
    Write-Output "Profile written to $outputPath"
}
finally {
    & $adb @adbTarget shell rm -f $remoteProbe 2>$null
}
