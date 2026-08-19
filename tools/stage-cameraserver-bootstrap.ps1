[CmdletBinding()]
param(
    [string]$AndroidSdk = "D:\AndroidSdk",
    [string]$Serial = "",
    [string]$ArtifactDirectory = "out\aosp14-router"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$adb = Join-Path $AndroidSdk "platform-tools\adb.exe"
$artifacts = Join-Path $repoRoot $ArtifactDirectory
$launcher = Join-Path $artifacts "vcam_cameraserver_launcher"
$router = Join-Path $artifacts "libvcam_cameraserver_router.so"
$remoteDirectory = "/data/local/tmp/android-vcam-bootstrap"
$adbTarget = @()
if ($Serial) { $adbTarget = @("-s", $Serial) }

foreach ($required in @($adb, $launcher, $router)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required staging input is missing: $required"
    }
}

& $adb @adbTarget get-state | Out-Null
if ($LASTEXITCODE -ne 0) { throw "ADB device is not connected" }

& $adb @adbTarget shell mkdir -p $remoteDirectory
if ($LASTEXITCODE -ne 0) { throw "Unable to create remote staging directory" }
& $adb @adbTarget push $launcher "$remoteDirectory/vcam_cameraserver_launcher" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Unable to stage cameraserver launcher" }
& $adb @adbTarget push $router "$remoteDirectory/libvcam_cameraserver_router.so" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Unable to stage cameraserver router" }
& $adb @adbTarget shell chmod 0755 "$remoteDirectory/vcam_cameraserver_launcher"
if ($LASTEXITCODE -ne 0) { throw "Unable to mark staged launcher executable" }
& $adb @adbTarget shell chmod 0644 "$remoteDirectory/libvcam_cameraserver_router.so"
if ($LASTEXITCODE -ne 0) { throw "Unable to set staged router permissions" }

$remoteHashes = & $adb @adbTarget shell sha256sum `
    "$remoteDirectory/vcam_cameraserver_launcher" `
    "$remoteDirectory/libvcam_cameraserver_router.so"
if ($LASTEXITCODE -ne 0) { throw "Unable to hash staged artifacts" }
$remoteHashes
Write-Output "Artifacts staged without mounting or restarting services: $remoteDirectory"
