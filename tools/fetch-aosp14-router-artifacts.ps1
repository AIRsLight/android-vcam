[CmdletBinding()]
param(
    [string]$CiHost = "ci@192.168.130.205",
    [string]$AospRoot = "/aosp/src/android-14.0.0_r23",
    [string]$OutputDirectory = "out\aosp14-router"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repoRoot $OutputDirectory
$intermediates = "$AospRoot/out/android-vcam-r23-soong/soong/.intermediates/vendor/android_vcam_buildcheck"
$remoteInputs = @{
    "vcam_cameraserver_launcher" = "$intermediates/vcam_cameraserver_launcher/android_arm64_armv8-a/vcam_cameraserver_launcher"
    "libvcam_cameraserver_router.so" = "$intermediates/libvcam_cameraserver_router/android_arm64_armv8-a_shared/libvcam_cameraserver_router.so"
}

function Assert-Arm64Elf([string]$Path, [int64]$MinimumSize) {
    $item = Get-Item -LiteralPath $Path
    if ($item.Length -lt $MinimumSize) { throw "ELF is unexpectedly small: $Path" }
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
        [BitConverter]::ToUInt16($header, 18) -ne 183) {
        throw "Expected a little-endian AArch64 ELF64 file: $Path"
    }
}

New-Item -ItemType Directory -Force $output | Out-Null
foreach ($entry in $remoteInputs.GetEnumerator()) {
    $destination = Join-Path $output $entry.Key
    & scp "$CiHost`:$($entry.Value)" $destination
    if ($LASTEXITCODE -ne 0) { throw "Unable to fetch $($entry.Key) from AOSP CI" }
}

Assert-Arm64Elf (Join-Path $output "vcam_cameraserver_launcher") 8192
Assert-Arm64Elf (Join-Path $output "libvcam_cameraserver_router.so") 8192

$manifest = foreach ($name in $remoteInputs.Keys | Sort-Object) {
    $path = Join-Path $output $name
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
    "$hash  $name"
}
Set-Content -LiteralPath (Join-Path $output "SHA256SUMS") -Value $manifest -Encoding ascii
$manifest
Write-Output "Fetched Android 14 router artifacts to $output"
