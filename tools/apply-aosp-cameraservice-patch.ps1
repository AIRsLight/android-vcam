[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$AospRoot,

    [ValidateSet("Check", "Apply")]
    [string]$Mode = "Check"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedAospRoot = (Resolve-Path -LiteralPath $AospRoot).Path
$frameworksAv = Join-Path $resolvedAospRoot "frameworks\av"
$patch = Join-Path $repoRoot "aosp\cameraservice\android-12\frameworks-av.patch"

foreach ($required in @(
    (Join-Path $frameworksAv ".git"),
    (Join-Path $frameworksAv "services\camera\libcameraservice\CameraService.cpp"),
    $patch
)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path not found: $required"
    }
}

$head = (& git -C $frameworksAv rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw "Unable to read frameworks/av revision" }

& git -C $frameworksAv apply --check $patch
if ($LASTEXITCODE -ne 0) {
    throw "Patch does not apply cleanly to frameworks/av HEAD $head"
}

if ($Mode -eq "Check") {
    Write-Output "Patch check passed for frameworks/av HEAD $head"
    Write-Output "No files were changed. Re-run with -Mode Apply to apply it."
    exit 0
}

& git -C $frameworksAv apply $patch
if ($LASTEXITCODE -ne 0) { throw "git apply failed" }

Write-Output "Applied VCAM CameraService integration to frameworks/av HEAD $head"
Write-Output "Next: add the product and SELinux snippets documented under aosp/cameraservice."
