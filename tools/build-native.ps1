[CmdletBinding()]
param(
    [string]$AndroidSdk = "D:\AndroidSdk",
    [string]$NdkVersion = "27.2.12479018",
    [string]$CmakeVersion = "3.22.1",
    [ValidateSet("arm64-v8a", "x86_64")]
    [string]$Abi = "arm64-v8a",
    [int]$Api = 31
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$ndkRoot = Join-Path $AndroidSdk "ndk\$NdkVersion"
$cmake = Join-Path $AndroidSdk "cmake\$CmakeVersion\bin\cmake.exe"
$ninja = Join-Path $AndroidSdk "cmake\$CmakeVersion\bin\ninja.exe"
$toolchain = Join-Path $ndkRoot "build\cmake\android.toolchain.cmake"
$buildDir = Join-Path $repoRoot "out\native\$Abi"

foreach ($required in @($cmake, $ninja, $toolchain)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required Android toolchain file not found: $required"
    }
}

& $cmake `
    -S (Join-Path $repoRoot "native") `
    -B $buildDir `
    -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DANDROID_ABI=$Abi" `
    "-DANDROID_PLATFORM=android-$Api" `
    "-DANDROID_STL=c++_static" `
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

& $cmake --build $buildDir --target camera_vcam vcam_proxy vcam_frame_publisher vcam_streamer vcam_control_daemon
if ($LASTEXITCODE -ne 0) { throw "Native build failed with exit code $LASTEXITCODE" }

$output = Join-Path $buildDir "camera.vcam.so"
if (-not (Test-Path -LiteralPath $output)) { throw "Expected output missing: $output" }
$publisher = Join-Path $buildDir "vcam-publisher"
if (-not (Test-Path -LiteralPath $publisher)) { throw "Expected output missing: $publisher" }
Write-Host "Built: $output"
Write-Host "Built: $(Join-Path $buildDir 'libvcam_proxy.so')"
Write-Host "Built: $publisher"
Write-Host "Built: $(Join-Path $buildDir 'vcam-streamer')"
Write-Host "Built: $(Join-Path $buildDir 'vcamd')"
