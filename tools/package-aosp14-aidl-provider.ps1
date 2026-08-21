[CmdletBinding()]
param(
    [string]$ArtifactRoot = "out/android14-provider-probe",
    [string]$NativeArtifactRoot = "out/native/arm64-v8a",
    [string]$Output = "dist/android-vcam-aidl-provider-v0.5.0-dev.33.zip",
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$sourceRoot = Split-Path -Parent $PSScriptRoot
$templateRoot = Join-Path $sourceRoot "aidl-provider-module"
$artifactRootPath = Join-Path $sourceRoot $ArtifactRoot
$nativeArtifactRootPath = Join-Path $sourceRoot $NativeArtifactRoot
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
$backendBinaries = @("vcam-streamer", "vcam-publisher", "vcamd")
$backendScripts = @("vcamctl", "provider-runner.sh", "device-probe.sh")

$required = @((Join-Path $artifactRootPath $binaryName))
$required += $libraries | ForEach-Object {
    Join-Path (Join-Path $artifactRootPath "lib64") $_
}
$required += $configFiles | ForEach-Object {
    Join-Path (Join-Path $artifactRootPath "config") $_
}
$required += $backendBinaries | ForEach-Object {
    Join-Path $nativeArtifactRootPath $_
}
$required += $backendScripts | ForEach-Object {
    Join-Path (Join-Path $sourceRoot "apmodule") $_
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
$systemBin = Join-Path $stagingRoot "system/bin"
$emptyConfig = Join-Path $stagingRoot "payload/empty-config"
$cameraConfig = Join-Path $stagingRoot "payload/camera-config"
New-Item -ItemType Directory -Force -Path $payloadBin, $payloadLib, $systemBin, $emptyConfig, $cameraConfig | Out-Null
Copy-Item -LiteralPath (Join-Path $artifactRootPath $binaryName) -Destination $payloadBin
foreach ($library in $libraries) {
    Copy-Item -LiteralPath (Join-Path (Join-Path $artifactRootPath "lib64") $library) `
        -Destination $payloadLib
}
foreach ($configFile in $configFiles) {
    Copy-Item -LiteralPath (Join-Path (Join-Path $artifactRootPath "config") $configFile) `
        -Destination $cameraConfig
}
Copy-Item -LiteralPath (Join-Path $nativeArtifactRootPath "vcam-streamer") -Destination $systemBin
Copy-Item -LiteralPath (Join-Path $nativeArtifactRootPath "vcamd") -Destination $systemBin
Copy-Item -LiteralPath (Join-Path $nativeArtifactRootPath "vcam-publisher") -Destination $systemBin
foreach ($script in $backendScripts) {
    Copy-Item -LiteralPath (Join-Path (Join-Path $sourceRoot "apmodule") $script) `
        -Destination $stagingRoot
}

$backendManifest = Join-Path $stagingRoot "payload/backend.sha256"
$backendPayloads = @(
    "system/bin/vcam-streamer",
    "system/bin/vcamd",
    "system/bin/vcam-publisher",
    "vcamctl",
    "provider-runner.sh",
    "device-probe.sh"
)
$manifestLines = foreach ($relativePath in $backendPayloads) {
    $payloadPath = Join-Path $stagingRoot $relativePath
    $payloadHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $payloadPath).Hash.ToLowerInvariant()
    "$payloadHash  $relativePath"
}
$manifestText = ($manifestLines -join "`n") + "`n"
[System.IO.File]::WriteAllText(
    $backendManifest, $manifestText, [System.Text.UTF8Encoding]::new($false))

$outputDirectory = Split-Path -Parent $outputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
if (Test-Path -LiteralPath $outputPath) {
    Remove-Item -LiteralPath $outputPath -Force
}
& $Python (Join-Path $PSScriptRoot "create-module-zip.py") $stagingRoot $outputPath
if ($LASTEXITCODE -ne 0) { throw "AIDL provider ZIP creation failed" }

$archive = Get-Item -LiteralPath $outputPath
$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $outputPath
Write-Output "Created $($archive.FullName)"
Write-Output "Size: $($archive.Length) bytes"
Write-Output "SHA-256: $($hash.Hash)"
