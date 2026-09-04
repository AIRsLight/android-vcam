[CmdletBinding()]
param(
    [string]$Python = "python",
    [string]$Output = "dist/android-vcam-capability-probe-v0.5.0-dev.39.zip"
)

$ErrorActionPreference = "Stop"
$sourceRoot = Split-Path -Parent $PSScriptRoot
$templateRoot = Join-Path $sourceRoot "capability-probe-module"
$outputPath = Join-Path $sourceRoot $Output
$stagingRoot = Join-Path $sourceRoot "out/capability-probe-package"
$expectedStagingParent = [System.IO.Path]::GetFullPath((Join-Path $sourceRoot "out"))
$resolvedStagingRoot = [System.IO.Path]::GetFullPath($stagingRoot)
if (-not $resolvedStagingRoot.StartsWith(
        $expectedStagingParent + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use staging directory outside the repository out directory"
}

if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null

foreach ($name in @(
        "module.prop",
        "skip_mount",
        "customize.sh",
        "run-probe.sh",
        "service.sh",
        "action.sh",
        "uninstall.sh",
        "README.md")) {
    Copy-Item -LiteralPath (Join-Path $templateRoot $name) -Destination $stagingRoot
}
Copy-Item -LiteralPath (Join-Path $sourceRoot "apmodule/device-probe.sh") `
    -Destination $stagingRoot

$outputDirectory = Split-Path -Parent $outputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
if (Test-Path -LiteralPath $outputPath) {
    Remove-Item -LiteralPath $outputPath -Force
}

& $Python (Join-Path $PSScriptRoot "create-module-zip.py") $stagingRoot $outputPath
if ($LASTEXITCODE -ne 0) {
    throw "Module archive creation failed"
}

& $Python (Join-Path $sourceRoot "tests/check_capability_probe_module.py") $outputPath
if ($LASTEXITCODE -ne 0) {
    throw "Capability probe archive validation failed"
}

$archive = Get-Item -LiteralPath $outputPath
$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $outputPath
Write-Output "Created $($archive.FullName)"
Write-Output "Size: $($archive.Length) bytes"
Write-Output "SHA-256: $($hash.Hash)"
