[CmdletBinding()]
param(
    [string]$Version = "0.5.0-dev.36",
    [string]$OriginalCameraHal = "out\device\camera.qcom.original.so",
    [string]$PatchedCameraHal = "out\device\camera.qcom.vcam-proxy.so",
    [string]$AidlArtifactRoot = "out/android14-provider-probe",
    [string]$NativeArtifactRoot = "out/native/arm64-v8a",
    [string]$RouterLauncher = "out\aosp14-router\vcam_cameraserver_launcher",
    [string]$RouterLibrary = "out\aosp14-router\libvcam_cameraserver_router.so"
)

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "package-release.ps1") `
    -OriginalCameraHal $OriginalCameraHal `
    -PatchedCameraHal $PatchedCameraHal `
    -Version $Version `
    -SkipDeviceModule
if ($LASTEXITCODE -ne 0) { throw "Common release packaging failed" }

& (Join-Path $PSScriptRoot "package-unified-module.ps1") `
    -Version $Version `
    -CameraHal $PatchedCameraHal `
    -AidlArtifactRoot $AidlArtifactRoot `
    -NativeArtifactRoot $NativeArtifactRoot `
    -RouterLauncher $RouterLauncher `
    -RouterLibrary $RouterLibrary
if ($LASTEXITCODE -ne 0) { throw "Unified root module packaging failed" }

& (Join-Path $PSScriptRoot "write-supported-release-manifest.ps1") -Version $Version
if ($LASTEXITCODE -ne 0) { throw "Release manifest generation failed" }
