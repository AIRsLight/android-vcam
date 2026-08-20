[CmdletBinding()]
param(
    [string]$ArtifactRoot = "out/android14-provider-probe",
    [string]$Output = "dist/android-vcam-aidl-provider-v0.5.0-dev.22.zip"
)

$ErrorActionPreference = "Stop"
$sourceRoot = Split-Path -Parent $PSScriptRoot
$templateRoot = Join-Path $sourceRoot "aidl-provider-module"
$artifactRootPath = Join-Path $sourceRoot $ArtifactRoot
$outputPath = Join-Path $sourceRoot $Output
$stagingRoot = Join-Path $sourceRoot "out/aidl-provider-package"
$expectedStagingParent = [System.IO.Path]::GetFullPath((Join-Path $sourceRoot "out"))
$resolvedStagingRoot = [System.IO.Path]::GetFullPath($stagingRoot)
if (-not $resolvedStagingRoot.StartsWith(
        $expectedStagingParent + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use staging directory outside the repository out directory"
}

$binaryName = "android.hardware.camera.provider-service-vcam-v2"
$libraries = @(
    "libvcam_googlecamerahwl_impl.so",
    "libgooglecamerahal.so",
    "libgooglecamerahalutils.so",
    "lib_profiler.so",
    "libgrallocusage.so",
    "libprotobuf-cpp-full-21.7.so"
)
$configFiles = @("emu_camera_back.json", "emu_camera_front.json")

$required = @((Join-Path $artifactRootPath $binaryName))
$required += $libraries | ForEach-Object {
    Join-Path (Join-Path $artifactRootPath "lib64") $_
}
$required += $configFiles | ForEach-Object {
    Join-Path (Join-Path $artifactRootPath "config") $_
}
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required AIDL provider artifact is missing: $path"
    }
}

if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null
Copy-Item -Path (Join-Path $templateRoot "*") -Destination $stagingRoot `
    -Recurse -Force

$payloadBin = Join-Path $stagingRoot "payload/bin"
$payloadLib = Join-Path $stagingRoot "payload/lib64"
$emptyConfig = Join-Path $stagingRoot "payload/empty-config"
$cameraConfig = Join-Path $stagingRoot "payload/camera-config"
New-Item -ItemType Directory -Force -Path $payloadBin, $payloadLib, $emptyConfig, $cameraConfig | Out-Null
Copy-Item -LiteralPath (Join-Path $artifactRootPath $binaryName) -Destination $payloadBin
foreach ($library in $libraries) {
    Copy-Item -LiteralPath (Join-Path (Join-Path $artifactRootPath "lib64") $library) `
        -Destination $payloadLib
}
foreach ($configFile in $configFiles) {
    Copy-Item -LiteralPath (Join-Path (Join-Path $artifactRootPath "config") $configFile) `
        -Destination $cameraConfig
}

$outputDirectory = Split-Path -Parent $outputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
if (Test-Path -LiteralPath $outputPath) {
    Remove-Item -LiteralPath $outputPath -Force
}
Compress-Archive -Path (Join-Path $stagingRoot "*") -DestinationPath $outputPath `
    -CompressionLevel Optimal

$archive = Get-Item -LiteralPath $outputPath
$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $outputPath
Write-Output "Created $($archive.FullName)"
Write-Output "Size: $($archive.Length) bytes"
Write-Output "SHA-256: $($hash.Hash)"
