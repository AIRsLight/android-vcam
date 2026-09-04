[CmdletBinding()]
param(
    [string]$ArtifactRoot = "out/android14-provider-probe",
    [string]$NativeArtifactRoot = "out/native/arm64-v8a",
    [string]$HttpsDownloader = "out/backend-java/vcam-https-downloader.jar",
    [string]$ConfigRoot = "",
    [string]$Output = "dist/android-vcam-aidl-provider-v0.5.0-dev.41.zip",
    [ValidateSet("nx769j", "aosp14-avd")]
    [string]$TargetProfile = "nx769j",
    [string]$Version = "0.5.0-dev.41",
    [int]$VersionCode = 53,
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$sourceRoot = Split-Path -Parent $PSScriptRoot
$templateRoot = Join-Path $sourceRoot "aidl-provider-module"
$usingBackend = $true
if ($TargetProfile -eq "aosp14-avd") {
    if (-not $PSBoundParameters.ContainsKey("ArtifactRoot")) {
        $ArtifactRoot = "out/aosp14-provider-x86_64"
    }
    if (-not $PSBoundParameters.ContainsKey("ConfigRoot")) {
        $ConfigRoot = "out/android14-provider-probe/config"
    }
    if (-not $PSBoundParameters.ContainsKey("NativeArtifactRoot")) {
        $NativeArtifactRoot = "out/native/x86_64"
    }
    if (-not $PSBoundParameters.ContainsKey("Output")) {
        $Output = "dist/android-vcam-aidl-provider-v0.5.0-dev.41-aosp14-avd.zip"
    }
}
if ([string]::IsNullOrWhiteSpace($ConfigRoot)) {
    $ConfigRoot = Join-Path $ArtifactRoot "config"
}
$artifactRootPath = Join-Path $sourceRoot $ArtifactRoot
$nativeArtifactRootPath = Join-Path $sourceRoot $NativeArtifactRoot
$httpsDownloaderPath = Join-Path $sourceRoot $HttpsDownloader
$configRootPath = Join-Path $sourceRoot $ConfigRoot
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
    Join-Path $configRootPath $_
}
if ($usingBackend) {
    $required += $backendBinaries | ForEach-Object {
        Join-Path $nativeArtifactRootPath $_
    }
    $required += $backendScripts | ForEach-Object {
        Join-Path (Join-Path $sourceRoot "apmodule") $_
    }
    $required += $httpsDownloaderPath
}
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required AIDL provider artifact is missing: $path"
    }
}

function Assert-TargetElf([string]$Path, [int]$ExpectedMachine) {
    $header = New-Object byte[] 20
    $stream = [IO.File]::OpenRead($Path)
    try {
        if ($stream.Read($header, 0, $header.Length) -ne $header.Length) {
            throw "Truncated ELF header: $Path"
        }
    }
    finally { $stream.Dispose() }
    if (-not ($header[0] -eq 0x7f -and $header[1] -eq 0x45 -and
              $header[2] -eq 0x4c -and $header[3] -eq 0x46) -or
        $header[4] -ne 2 -or $header[5] -ne 1 -or
        [BitConverter]::ToUInt16($header, 18) -ne $ExpectedMachine) {
        throw "Expected a little-endian ELF64 machine $ExpectedMachine file: $Path"
    }
}

$expectedMachine = if ($TargetProfile -eq "aosp14-avd") { 62 } else { 183 }
Assert-TargetElf (Join-Path $artifactRootPath $binaryName) $expectedMachine
if ($usingBackend) {
    foreach ($backendBinary in $backendBinaries) {
        Assert-TargetElf (Join-Path $nativeArtifactRootPath $backendBinary) $expectedMachine
    }
}

if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null
Copy-Item -Path (Join-Path $templateRoot "*") -Destination $stagingRoot `
    -Recurse -Force

$moduleProp = Join-Path $stagingRoot "module.prop"
$modulePropText = [System.IO.File]::ReadAllText($moduleProp)
$modulePropText = [Text.RegularExpressions.Regex]::Replace(
    $modulePropText, "(?m)^version=.*$", "version=$Version")
$modulePropText = [Text.RegularExpressions.Regex]::Replace(
    $modulePropText, "(?m)^versionCode=.*$", "versionCode=$VersionCode")
[System.IO.File]::WriteAllText(
    $moduleProp, $modulePropText, [System.Text.UTF8Encoding]::new($false))

$payloadBin = Join-Path $stagingRoot "payload/bin"
$payloadLib = Join-Path $stagingRoot "payload/lib64"
$systemBin = Join-Path $stagingRoot "system/bin"
$systemFramework = Join-Path $stagingRoot "system/framework"
$emptyConfig = Join-Path $stagingRoot "payload/empty-config"
$cameraConfig = Join-Path $stagingRoot "payload/camera-config"
New-Item -ItemType Directory -Force -Path $payloadBin, $payloadLib, $systemBin, $systemFramework, $emptyConfig, $cameraConfig | Out-Null
Copy-Item -LiteralPath (Join-Path $artifactRootPath $binaryName) -Destination $payloadBin
foreach ($library in $libraries) {
    Copy-Item -LiteralPath (Join-Path (Join-Path $artifactRootPath "lib64") $library) `
        -Destination $payloadLib
}
foreach ($configFile in $configFiles) {
    Copy-Item -LiteralPath (Join-Path $configRootPath $configFile) `
        -Destination $cameraConfig
}
if ($usingBackend) {
    Copy-Item -LiteralPath (Join-Path $nativeArtifactRootPath "vcam-streamer") -Destination $systemBin
    Copy-Item -LiteralPath (Join-Path $nativeArtifactRootPath "vcamd") -Destination $systemBin
    Copy-Item -LiteralPath (Join-Path $nativeArtifactRootPath "vcam-publisher") -Destination $systemBin
    Copy-Item -LiteralPath $httpsDownloaderPath -Destination $systemFramework
    foreach ($script in $backendScripts) {
        Copy-Item -LiteralPath (Join-Path (Join-Path $sourceRoot "apmodule") $script) `
            -Destination $stagingRoot
    }
}

$backendManifest = Join-Path $stagingRoot "payload/backend.sha256"
$backendPayloads = @(
    "system/bin/vcam-streamer",
    "system/bin/vcamd",
    "system/bin/vcam-publisher",
    "system/framework/vcam-https-downloader.jar",
    "vcamctl",
    "provider-runner.sh",
    "device-probe.sh"
)
if ($usingBackend) {
    $manifestLines = foreach ($relativePath in $backendPayloads) {
        $payloadPath = Join-Path $stagingRoot $relativePath
        $payloadHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $payloadPath).Hash.ToLowerInvariant()
        "$payloadHash  $relativePath"
    }
    $manifestText = ($manifestLines -join "`n") + "`n"
    [System.IO.File]::WriteAllText(
        $backendManifest, $manifestText, [System.Text.UTF8Encoding]::new($false))
}
if ($TargetProfile -eq "aosp14-avd") {
    [System.IO.File]::WriteAllText(
        (Join-Path $stagingRoot "profile.id"),
        "aosp14-avd-api34-ue1a-r23`n",
        [System.Text.UTF8Encoding]::new($false))
    $modulePropText = [System.IO.File]::ReadAllText($moduleProp)
    $modulePropText = [Text.RegularExpressions.Regex]::Replace(
        $modulePropText,
        "(?m)^description=.*$",
        "description=One-shot Android 14 API 34 x86_64 AVD stable-AIDL provider and media backend harness.")
    [System.IO.File]::WriteAllText(
        $moduleProp, $modulePropText, [System.Text.UTF8Encoding]::new($false))
}

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
