[CmdletBinding()]
param(
    [string]$AndroidSdk = "D:\AndroidSdk",
    [string]$BuildToolsVersion = "35.0.0",
    [int]$CompileSdk = 35,
    [string]$NdkVersion = "27.2.12479018",
    [string]$JdkHome = "$env:LOCALAPPDATA\Programs\Microsoft\jdk-17.0.10.7-hotspot",
    [string]$Version = "0.5.0-dev.39",
    [int]$VersionCode = 23
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$ndkToolchain = Join-Path $AndroidSdk "ndk\$NdkVersion\toolchains\llvm\prebuilt\windows-x86_64\bin"
$nativePackage = Join-Path $repoRoot "out\testapp\native-package"
$nativeCompilers = [ordered]@{
    "arm64-v8a" = "aarch64-linux-android29-clang++.cmd"
    "x86_64" = "x86_64-linux-android29-clang++.cmd"
}
foreach ($abi in $nativeCompilers.Keys) {
    $clang = Join-Path $ndkToolchain $nativeCompilers[$abi]
    if (-not (Test-Path -LiteralPath $clang)) {
        throw "Required Android NDK compiler not found: $clang"
    }
    $nativeAbi = Join-Path $nativePackage "lib\$abi"
    New-Item -ItemType Directory -Force -Path $nativeAbi | Out-Null
    $nativeOutput = Join-Path $nativeAbi "libvcam_protocol_probe.so"
    & $clang -shared -fPIC -static-libstdc++ -std=c++17 -Wall -Wextra -Werror `
        (Join-Path $repoRoot "testapp\jni\protocol_probe_jni.cpp") `
        -lcamera2ndk -llog -o $nativeOutput
    if ($LASTEXITCODE -ne 0) { throw "Protocol probe JNI build failed for $abi" }
}

& (Join-Path $PSScriptRoot "build-manager.ps1") `
    -AndroidSdk $AndroidSdk `
    -BuildToolsVersion $BuildToolsVersion `
    -CompileSdk $CompileSdk `
    -MinSdk 29 `
    -JdkHome $JdkHome `
    -AppDirectory "testapp" `
    -BuildDirectory "out\testapp" `
    -OutputFile "android-vcam-camera2-test-debug.apk" `
    -NativeLibDirectory $nativePackage `
    -Version $Version `
    -VersionCode $VersionCode
if ($LASTEXITCODE -ne 0) { throw "Camera2 test APK build failed" }
