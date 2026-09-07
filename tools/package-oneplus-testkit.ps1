[CmdletBinding()]
param(
    [string]$Version = "0.5.0-dev.42",
    [int]$VersionCode = 62
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $repoRoot "dist"
$bundleRoot = Join-Path $repoRoot "out/oneplus-testkit"

& (Join-Path $PSScriptRoot "build-native.ps1") -Api 29 -Abi arm64-v8a
if ($LASTEXITCODE -ne 0) { throw "Native build failed" }
& (Join-Path $PSScriptRoot "build-manager.ps1") -MinSdk 29 -Version $Version -VersionCode $VersionCode
if ($LASTEXITCODE -ne 0) { throw "Manager build failed" }
& (Join-Path $PSScriptRoot "build-testapp.ps1") -Version $Version -VersionCode $VersionCode
if ($LASTEXITCODE -ne 0) { throw "Test app build failed" }
& (Join-Path $PSScriptRoot "build-backend-java.ps1")
if ($LASTEXITCODE -ne 0) { throw "Backend helper build failed" }
& (Join-Path $PSScriptRoot "package-oneplus-global-module.ps1") -Version $Version -VersionCode $VersionCode
if ($LASTEXITCODE -ne 0) { throw "Module package failed" }

# Only replace this tool's dedicated staging directory beneath out.
if (Test-Path -LiteralPath $bundleRoot) {
    $resolved = (Resolve-Path -LiteralPath $bundleRoot).Path
    $expected = [IO.Path]::GetFullPath((Join-Path $repoRoot "out/oneplus-testkit"))
    if ($resolved -ne $expected) { throw "Unexpected staging path: $resolved" }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Force $bundleRoot | Out-Null
$artifacts = [ordered]@{
    "android-vcam-oneplus-global-v$Version.zip" = "dist/android-vcam-oneplus-global-v$Version.zip"
    "android-vcam-manager-v$Version.apk" = "out/manager/android-vcam-manager-debug.apk"
    "android-vcam-camera2-test-v$Version.apk" = "out/testapp/android-vcam-camera2-test-debug.apk"
    "TESTING.md" = "docs/community-testing.md"
}
$manifestArtifacts = foreach ($name in $artifacts.Keys) {
    $source = Join-Path $repoRoot $artifacts[$name]
    Copy-Item -LiteralPath $source -Destination (Join-Path $bundleRoot $name)
    [ordered]@{ name = $name; sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$manifest = [ordered]@{
    schema = 1
    version = $Version
    source_commit = (git -C $repoRoot rev-parse HEAD)
    source_dirty = [bool](git -C $repoRoot status --porcelain)
    qualification = "experimental-community-testing"
    scope = "OnePlus Qualcomm Android 10-14 global adapter"
    manager_min_sdk = 29
    artifacts = @($manifestArtifacts)
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bundleRoot "manifest.json") -Encoding utf8
$output = Join-Path $dist "android-vcam-oneplus-testkit-v$Version.zip"
Compress-Archive -Path (Join-Path $bundleRoot "*") -DestinationPath $output -Force
$hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$output.sha256" -Value "$hash  $(Split-Path -Leaf $output)" -Encoding ascii
Write-Output "Created $output"
Write-Output "SHA-256 $hash"
