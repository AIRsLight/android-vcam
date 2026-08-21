[CmdletBinding()]
param(
    [string]$Version = "0.5.0-dev.38",
    [string]$OutputDirectory = "dist"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $repoRoot $OutputDirectory
$artifactNames = @(
    "android-vcam-manager-v$Version-debug.apk",
    "android-vcam-camera2-test-v$Version-debug.apk",
    "android-vcam-module-v$Version.zip"
)

$artifacts = foreach ($name in $artifactNames) {
    $path = Join-Path $dist $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Expected release artifact is missing: $path"
    }
    $item = Get-Item -LiteralPath $path
    [ordered]@{
        file = $name
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
        bytes = $item.Length
    }
}

$manifest = [ordered]@{
    schema = 2
    release = $Version
    selection = "one root module auto-selects an exact fingerprint and camera ABI; unknown builds fail closed"
    common = [ordered]@{
        manager = "android-vcam-manager-v$Version-debug.apk"
        test_app = "android-vcam-camera2-test-v$Version-debug.apk"
    }
    profiles = @(
        [ordered]@{
            id = "oneplus7pro-p202303230244"
            fingerprint = "OnePlus/OnePlus7Pro_CH/OnePlus7Pro:12/SKQ1.211113.001/P.202303230244:user/release-keys"
            root_delivery = "APatch with an active supported MetaModule"
            modules = @("android-vcam-module-v$Version.zip")
        },
        [ordered]@{
            id = "nx769j-ukq1-20240417"
            fingerprint = "nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys"
            root_delivery = "KernelSU with an active supported MetaModule"
            modules = @("android-vcam-module-v$Version.zip")
        }
    )
    artifacts = @($artifacts)
}

$manifestPath = Join-Path $dist "android-vcam-supported-v$Version.json"
$manifestJson = ($manifest | ConvertTo-Json -Depth 6) + "`n"
[IO.File]::WriteAllText($manifestPath, $manifestJson, [Text.UTF8Encoding]::new($false))

Write-Output "Created $manifestPath"
Write-Output "Release $Version contains $($artifacts.Count) verified artifacts"
