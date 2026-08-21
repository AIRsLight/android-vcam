[CmdletBinding()]
param(
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
    -PatchedCameraHal $PatchedCameraHal
if ($LASTEXITCODE -ne 0) { throw "OnePlus 7 Pro release packaging failed" }

& (Join-Path $PSScriptRoot "package-aosp14-aidl-provider.ps1") `
    -ArtifactRoot $AidlArtifactRoot `
    -NativeArtifactRoot $NativeArtifactRoot
if ($LASTEXITCODE -ne 0) { throw "NX769J AIDL provider packaging failed" }

& (Join-Path $PSScriptRoot "package-portable-bootstrap.ps1") `
    -Launcher $RouterLauncher `
    -Router $RouterLibrary `
    -BootstrapMode physical-route
if ($LASTEXITCODE -ne 0) { throw "NX769J router packaging failed" }

& (Join-Path $PSScriptRoot "write-supported-release-manifest.ps1")
if ($LASTEXITCODE -ne 0) { throw "Release manifest generation failed" }
