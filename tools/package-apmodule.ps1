[CmdletBinding()]
param(
    [string]$CameraHal = "out\device\camera.qcom.vcam-proxy.so",
    [string]$ProxyLibrary = "out\native\arm64-v8a\libvcam_proxy.so",
    [string]$FramePublisher = "out\native\arm64-v8a\vcam-publisher",
    [string]$StreamProvider = "out\native\arm64-v8a\vcam-streamer",
    [string]$ControlDaemon = "out\native\arm64-v8a\vcamd",
    [string]$CameraService = "out\device\libcameraservice.vcam.so",
    [string]$OutputDirectory = "dist",
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Resolve-RepoInput([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $root $Path }
    return (Resolve-Path -LiteralPath $Path).ProviderPath
}

function Assert-Arm64Elf([string]$Path, [int64]$MinimumSize) {
    $info = Get-Item -LiteralPath $Path
    if ($info.Length -lt $MinimumSize) { throw "ELF is unexpectedly small: $Path" }
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
        throw "Expected a little-endian AArch64 ELF64 file: $Path"
    }
}

$inputs = @{
    Hal = Resolve-RepoInput $CameraHal
    Proxy = Resolve-RepoInput $ProxyLibrary
    Publisher = Resolve-RepoInput $FramePublisher
    Streamer = Resolve-RepoInput $StreamProvider
    Daemon = Resolve-RepoInput $ControlDaemon
    CameraService = Resolve-RepoInput $CameraService
}
Assert-Arm64Elf $inputs.Hal 65536
Assert-Arm64Elf $inputs.Proxy 65536
Assert-Arm64Elf $inputs.Publisher 8192
Assert-Arm64Elf $inputs.Streamer 8192
Assert-Arm64Elf $inputs.Daemon 8192
Assert-Arm64Elf $inputs.CameraService 65536

$dist = Join-Path $root $OutputDirectory
New-Item -ItemType Directory -Force $dist | Out-Null
$staging = Join-Path $dist "android-vcam-apm-staging"
if (Test-Path -LiteralPath $staging) {
    $resolvedStaging = (Resolve-Path -LiteralPath $staging).ProviderPath
    if (-not $resolvedStaging.StartsWith((Resolve-Path $dist).ProviderPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace staging outside dist: $resolvedStaging"
    }
    Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
}
Copy-Item -LiteralPath (Join-Path $root "apmodule") -Destination $staging -Recurse

$destinations = @{
    Hal = Join-Path $staging "vendor/lib64/hw/camera.qcom.so"
    Proxy = Join-Path $staging "vendor/lib64/libvcam_proxy.so"
    Publisher = Join-Path $staging "vendor/bin/vcam-publisher"
    Streamer = Join-Path $staging "system/bin/vcam-streamer"
    Daemon = Join-Path $staging "system/bin/vcamd"
    CameraService = Join-Path $staging "system/lib64/libcameraservice.so"
}
foreach ($destination in $destinations.Values) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
}
$legacyVendorTree = Join-Path $staging "system/vendor"
if (Test-Path -LiteralPath $legacyVendorTree) {
    Remove-Item -LiteralPath $legacyVendorTree -Recurse -Force
}
Copy-Item -LiteralPath $inputs.Hal -Destination $destinations.Hal -Force
Copy-Item -LiteralPath $inputs.Proxy -Destination $destinations.Proxy -Force
Copy-Item -LiteralPath $inputs.Publisher -Destination $destinations.Publisher -Force
Copy-Item -LiteralPath $inputs.Streamer -Destination $destinations.Streamer -Force
Copy-Item -LiteralPath $inputs.Daemon -Destination $destinations.Daemon -Force
Copy-Item -LiteralPath $inputs.CameraService -Destination $destinations.CameraService -Force
Get-ChildItem -LiteralPath $staging -Recurse -Filter .gitkeep | Remove-Item -Force

$zip = Join-Path $dist "android-vcam-oneplus7pro-apm-v0.5.0-dev.32.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
& $Python (Join-Path $PSScriptRoot "create-module-zip.py") $staging $zip
if ($LASTEXITCODE -ne 0) { throw "APatch ZIP creation failed" }
$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $zip
Set-Content -LiteralPath "$zip.sha256" `
    -Value ($hash.Hash.ToLowerInvariant() + "  " + (Split-Path -Leaf $zip)) -Encoding ascii
Remove-Item -LiteralPath $staging -Recurse -Force

Write-Output "Created $zip"
Write-Output "SHA-256 $($hash.Hash.ToLowerInvariant())"
