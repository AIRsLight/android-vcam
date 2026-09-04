[CmdletBinding()]
param(
    [string]$NativeArtifactRoot = "out/native/arm64-v8a",
    [string]$HttpsDownloader = "out/backend-java/vcam-https-downloader.jar",
    [string]$OutputDirectory = "dist",
    [string]$Python = "python",
    [string]$Version = "0.5.0-dev.40"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outRoot = Join-Path $repoRoot "out"
$staging = Join-Path $outRoot "oneplus-global-module-staging"
$nativeRoot = Join-Path $repoRoot $NativeArtifactRoot

function Assert-Arm64Elf([string]$Path, [int64]$MinimumSize) {
    $item = Get-Item -LiteralPath $Path
    if ($item.Length -lt $MinimumSize) { throw "ELF is unexpectedly small: $Path" }
    $header = New-Object byte[] 20
    $stream = [IO.File]::OpenRead($Path)
    try {
        if ($stream.Read($header, 0, $header.Length) -ne $header.Length) {
            throw "Truncated ELF header: $Path"
        }
    } finally { $stream.Dispose() }
    if (-not ($header[0] -eq 0x7f -and $header[1] -eq 0x45 -and
              $header[2] -eq 0x4c -and $header[3] -eq 0x46) -or
        $header[4] -ne 2 -or $header[5] -ne 1 -or
        [BitConverter]::ToUInt16($header, 18) -ne 183) {
        throw "Expected an AArch64 ELF64 file: $Path"
    }
}

if (Test-Path -LiteralPath $staging) {
    $resolved = [IO.Path]::GetFullPath($staging)
    $allowed = [IO.Path]::GetFullPath($outRoot) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace staging outside out: $resolved"
    }
    Remove-Item -LiteralPath $staging -Recurse -Force
}
Copy-Item -LiteralPath (Join-Path $repoRoot "oneplus-global-module") `
    -Destination $staging -Recurse

$sharedFiles = @(
    "action.sh", "boot-completed.sh", "device-probe.sh", "provider-runner.sh",
    "tls-ca.sh", "vcamctl"
)
foreach ($name in $sharedFiles) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "apmodule/$name") -Destination $staging
}
Copy-Item -LiteralPath (Join-Path $repoRoot "apmodule/webroot") `
    -Destination (Join-Path $staging "webroot") -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot "THIRD_PARTY_NOTICES.md") -Destination $staging

$inputs = @{
    Shim = Join-Path $nativeRoot "camera.qcom.so"
    Publisher = Join-Path $nativeRoot "vcam-publisher"
    Streamer = Join-Path $nativeRoot "vcam-streamer"
    Daemon = Join-Path $nativeRoot "vcamd"
    HttpsDownloader = Join-Path $repoRoot $HttpsDownloader
}
Assert-Arm64Elf $inputs.Shim 65536
Assert-Arm64Elf $inputs.Publisher 8192
Assert-Arm64Elf $inputs.Streamer 8192
Assert-Arm64Elf $inputs.Daemon 8192

$destinations = @{
    Shim = Join-Path $staging "system/vendor/lib64/hw/camera.qcom.so"
    Publisher = Join-Path $staging "system/vendor/bin/vcam-publisher"
    Streamer = Join-Path $staging "system/bin/vcam-streamer"
    Daemon = Join-Path $staging "system/bin/vcamd"
    HttpsDownloader = Join-Path $staging "system/framework/vcam-https-downloader.jar"
}
foreach ($destination in $destinations.Values) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
}
foreach ($key in $inputs.Keys) {
    Copy-Item -LiteralPath $inputs[$key] -Destination $destinations[$key] -Force
}

$moduleProp = Join-Path $staging "module.prop"
$moduleText = [IO.File]::ReadAllText($moduleProp)
$moduleText = [Text.RegularExpressions.Regex]::Replace(
    $moduleText, "(?m)^version=.*$", "version=$Version")
[IO.File]::WriteAllText($moduleProp, $moduleText, [Text.UTF8Encoding]::new($false))

$dist = Join-Path $repoRoot $OutputDirectory
New-Item -ItemType Directory -Force $dist | Out-Null
$zip = Join-Path $dist "android-vcam-oneplus-global-v$Version.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
& $Python (Join-Path $PSScriptRoot "create-module-zip.py") $staging $zip
if ($LASTEXITCODE -ne 0) { throw "OnePlus global module ZIP creation failed" }
& $Python (Join-Path $repoRoot "tests/check_oneplus_global_module.py") $zip
if ($LASTEXITCODE -ne 0) { throw "OnePlus global module validation failed" }
$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $zip
Set-Content -LiteralPath "$zip.sha256" `
    -Value ($hash.Hash.ToLowerInvariant() + "  " + (Split-Path -Leaf $zip)) -Encoding ascii
Remove-Item -LiteralPath $staging -Recurse -Force

Write-Output "Created $zip"
Write-Output "SHA-256 $($hash.Hash.ToLowerInvariant())"
