[CmdletBinding()]
param(
    [string]$ArtifactRoot = "out/android14-provider-probe",
    [string]$Output = "dist/android-vcam-provider-probe-v0.5.0-dev.13.zip"
)

$ErrorActionPreference = "Stop"
$sourceRoot = Split-Path -Parent $PSScriptRoot
$templateRoot = Join-Path $sourceRoot "provider-probe-module"
$artifactRootPath = Join-Path $sourceRoot $ArtifactRoot
$outputPath = Join-Path $sourceRoot $Output
$stagingRoot = Join-Path $sourceRoot "out/provider-probe-package"
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
    "libprotobuf-cpp-full-21.7.so",
    "libprovider_probe_trace.so"
)

$required = @(
    (Join-Path $artifactRootPath $binaryName)
)
$required += $libraries | ForEach-Object {
    Join-Path (Join-Path $artifactRootPath "lib64") $_
}
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required provider probe artifact is missing: $path"
    }
}

if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null
$templateFiles = @(
    "module.prop",
    "skip_mount",
    "sepolicy.rule",
    "customize.sh",
    "action.sh",
    "uninstall.sh",
    "README.md"
)
foreach ($templateFile in $templateFiles) {
    Copy-Item -LiteralPath (Join-Path $templateRoot $templateFile) `
        -Destination $stagingRoot
}

$payloadBin = Join-Path $stagingRoot "payload/bin"
$payloadLib = Join-Path $stagingRoot "payload/lib64"
$emptyConfig = Join-Path $stagingRoot "payload/empty-config"
New-Item -ItemType Directory -Force -Path $payloadBin, $payloadLib, $emptyConfig | Out-Null
Copy-Item -LiteralPath (Join-Path $artifactRootPath $binaryName) -Destination $payloadBin
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
