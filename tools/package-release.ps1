[CmdletBinding()]
param(
    [string]$OriginalCameraHal = "out\device\camera.qcom.original.so",
    [string]$PatchedCameraHal = "out\device\camera.qcom.vcam-proxy.so",
    [string]$ManagerApk = "out\manager\android-vcam-manager-debug.apk",
    [string]$TestApk = "out\testapp\android-vcam-camera2-test-debug.apk"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$original = Join-Path $repoRoot $OriginalCameraHal
$patched = Join-Path $repoRoot $PatchedCameraHal
$managerPath = Join-Path $repoRoot $ManagerApk
$apkPath = Join-Path $repoRoot $TestApk
$dist = Join-Path $repoRoot "dist"

& (Join-Path $PSScriptRoot "build-native.ps1")
if ($LASTEXITCODE -ne 0) { throw "Native build failed" }
& (Join-Path $PSScriptRoot "build-manager.ps1")
if ($LASTEXITCODE -ne 0) { throw "Manager APK build failed" }
& (Join-Path $PSScriptRoot "build-testapp.ps1")
if ($LASTEXITCODE -ne 0) { throw "Test APK build failed" }
python (Join-Path $PSScriptRoot "patch-original-hal.py") `
    --library /vendor/lib64/hw/local_time.default.so $original $patched
if ($LASTEXITCODE -ne 0) { throw "OEM HAL patch failed" }

& (Join-Path $PSScriptRoot "package-apmodule.ps1") -CameraHal $patched
if ($LASTEXITCODE -ne 0) { throw "APatch module packaging failed" }

$releaseManager = Join-Path $dist "android-vcam-manager-v0.3.5-dev-debug.apk"
Copy-Item -LiteralPath $managerPath -Destination $releaseManager -Force
$managerHash = Get-FileHash -Algorithm SHA256 -LiteralPath $releaseManager
Set-Content -LiteralPath "$releaseManager.sha256" `
    -Value ($managerHash.Hash.ToLowerInvariant() + "  " + (Split-Path -Leaf $releaseManager)) `
    -Encoding ascii

$releaseApk = Join-Path $dist "android-vcam-camera2-test-v0.3.5-dev-debug.apk"
Copy-Item -LiteralPath $apkPath -Destination $releaseApk -Force
$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $releaseApk
Set-Content -LiteralPath "$releaseApk.sha256" `
    -Value ($hash.Hash.ToLowerInvariant() + "  " + (Split-Path -Leaf $releaseApk)) `
    -Encoding ascii

Write-Output "Created $releaseApk"
Write-Output "SHA-256 $($hash.Hash.ToLowerInvariant())"
Write-Output "Created $releaseManager"
Write-Output "SHA-256 $($managerHash.Hash.ToLowerInvariant())"
