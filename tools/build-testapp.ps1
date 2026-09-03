[CmdletBinding()]
param(
    [string]$AndroidSdk = "D:\AndroidSdk",
    [string]$BuildToolsVersion = "35.0.0",
    [int]$CompileSdk = 35,
    [string]$JdkHome = "$env:LOCALAPPDATA\Programs\Microsoft\jdk-17.0.10.7-hotspot",
    [string]$Version = "0.5.0-dev.39",
    [int]$VersionCode = 23
)

$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "build-manager.ps1") `
    -AndroidSdk $AndroidSdk `
    -BuildToolsVersion $BuildToolsVersion `
    -CompileSdk $CompileSdk `
    -JdkHome $JdkHome `
    -AppDirectory "testapp" `
    -BuildDirectory "out\testapp" `
    -OutputFile "android-vcam-camera2-test-debug.apk" `
    -Version $Version `
    -VersionCode $VersionCode
if ($LASTEXITCODE -ne 0) { throw "Camera2 test APK build failed" }
