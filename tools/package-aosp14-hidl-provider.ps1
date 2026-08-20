[CmdletBinding()]
param(
    [string]$ArtifactRoot = "out/android14-hidl-probe",
    [string]$Output = "dist/android-vcam-hidl-provider-v0.5.0-dev.15.zip"
)

$ErrorActionPreference = "Stop"
$sourceRoot = Split-Path -Parent $PSScriptRoot
$templateRoot = Join-Path $sourceRoot "hidl-provider-module"
$artifactRootPath = Join-Path $sourceRoot $ArtifactRoot
$outputPath = Join-Path $sourceRoot $Output
$stagingRoot = Join-Path $sourceRoot "out/hidl-provider-package"
$expectedStagingParent = [System.IO.Path]::GetFullPath((Join-Path $sourceRoot "out"))
$resolvedStagingRoot = [System.IO.Path]::GetFullPath($stagingRoot)
if (-not $resolvedStagingRoot.StartsWith(
        $expectedStagingParent + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use staging directory outside the repository out directory"
}

$binaryName = "vcam_hidl_provider"
$cameraModuleName = "camera.vcam.so"
$libraries = @(
    "android.hardware.camera.common@1.0.so",
    "android.hardware.camera.device@1.0.so",
    "android.hardware.camera.device@3.2.so",
    "android.hardware.camera.device@3.3.so",
    "android.hardware.camera.device@3.4.so",
    "android.hardware.camera.provider@2.4.so",
    "android.hardware.graphics.mapper@2.0.so",
    "android.hardware.graphics.mapper@3.0.so",
    "android.hardware.graphics.mapper@4.0.so",
    "android.hidl.allocator@1.0.so",
    "android.hidl.memory@1.0.so",
    "camera.device@3.2-impl.so",
    "camera.device@3.3-impl.so",
    "camera.device@3.4-impl.so"
)

$required = @(
    (Join-Path $artifactRootPath $binaryName),
    (Join-Path $artifactRootPath $cameraModuleName)
)
$required += $libraries | ForEach-Object {
    Join-Path (Join-Path $artifactRootPath "lib64") $_
}
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required HIDL provider artifact is missing: $path"
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
$payloadHw = Join-Path $payloadLib "hw"
New-Item -ItemType Directory -Force -Path $payloadBin, $payloadLib, $payloadHw | Out-Null
Copy-Item -LiteralPath (Join-Path $artifactRootPath $binaryName) -Destination $payloadBin
Copy-Item -LiteralPath (Join-Path $artifactRootPath $cameraModuleName) -Destination $payloadHw
foreach ($library in $libraries) {
    Copy-Item -LiteralPath (Join-Path (Join-Path $artifactRootPath "lib64") $library) `
        -Destination $payloadLib
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
