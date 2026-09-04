[CmdletBinding()]
param(
    [string]$Launcher = "out\aosp14-router\vcam_cameraserver_launcher",
    [string]$Router = "out\aosp14-router\libvcam_cameraserver_router.so",
    [string]$OutputDirectory = "dist",
    [ValidateSet("stock", "preflight", "passthrough", "physical-route")]
    [string]$BootstrapMode = "stock",
    [ValidateSet("nx769j", "aosp14-avd")]
    [string]$TargetProfile = "nx769j",
    [string]$Python = "python",
    [string]$Version = "0.5.0-dev.39"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-RepoInput([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $repoRoot $Path }
    return (Resolve-Path -LiteralPath $Path).ProviderPath
}

function Assert-TargetElf([string]$Path, [int64]$MinimumSize, [int]$ExpectedMachine) {
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
        [BitConverter]::ToUInt16($header, 18) -ne $ExpectedMachine) {
        throw "Expected a little-endian ELF64 machine $ExpectedMachine file: $Path"
    }
}

$launcherInput = Resolve-RepoInput $Launcher
$routerInput = Resolve-RepoInput $Router
$expectedMachine = if ($TargetProfile -eq "aosp14-avd") { 62 } else { 183 }
Assert-TargetElf $launcherInput 8192 $expectedMachine
Assert-TargetElf $routerInput 8192 $expectedMachine

$dist = Join-Path $repoRoot $OutputDirectory
New-Item -ItemType Directory -Force $dist | Out-Null
$staging = Join-Path $dist "android-vcam-portable-staging"
if (Test-Path -LiteralPath $staging) {
    $resolvedStaging = (Resolve-Path -LiteralPath $staging).ProviderPath
    $resolvedDist = (Resolve-Path -LiteralPath $dist).ProviderPath
    if (-not $resolvedStaging.StartsWith($resolvedDist, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace staging outside dist: $resolvedStaging"
    }
    Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
}
Copy-Item -LiteralPath (Join-Path $repoRoot "portable-module") -Destination $staging -Recurse
$profileProp = Join-Path $staging "module.prop"
$profilePropText = [IO.File]::ReadAllText($profileProp)
$profilePropText = [Text.RegularExpressions.Regex]::Replace(
    $profilePropText, "(?m)^version=.*$", "version=$Version")
[IO.File]::WriteAllText($profileProp, $profilePropText, [Text.UTF8Encoding]::new($false))
if ($TargetProfile -eq "aosp14-avd") {
    [IO.File]::WriteAllText(
        (Join-Path $staging "profile.id"),
        "aosp14-avd-api34-ue1a-r23`n",
        [Text.UTF8Encoding]::new($false))
    $profilePropText = [IO.File]::ReadAllText($profileProp)
    $profilePropText = [Text.RegularExpressions.Regex]::Replace(
        $profilePropText,
        "(?m)^description=.*$",
        "description=One-shot Android 14 API 34 x86_64 AVD CameraService bootstrap harness.")
    [IO.File]::WriteAllText(
        $profileProp, $profilePropText, [Text.UTF8Encoding]::new($false))
}

$launcherDestination = Join-Path $staging "system\bin\cameraserver"
$routerDestination = Join-Path $staging "system\lib64\libvcam_cameraserver_router.so"
Copy-Item -LiteralPath $launcherInput -Destination $launcherDestination -Force
Copy-Item -LiteralPath $routerInput -Destination $routerDestination -Force
$modeDestination = Join-Path $staging "system\etc\android_vcam\bootstrap.mode"
[System.IO.File]::WriteAllText(
    $modeDestination, "$BootstrapMode`n", [System.Text.UTF8Encoding]::new($false))
Get-ChildItem -LiteralPath $staging -Recurse -Filter .gitkeep | Remove-Item -Force

$modeSuffix = if ($BootstrapMode -eq "stock") { "" } else { "-$BootstrapMode" }
$profileSuffix = if ($TargetProfile -eq "aosp14-avd") { "-aosp14-avd" } else { "" }
$zip = Join-Path $dist "android-vcam-portable-bootstrap-v$Version$profileSuffix$modeSuffix.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
& $Python (Join-Path $PSScriptRoot "create-module-zip.py") $staging $zip
if ($LASTEXITCODE -ne 0) { throw "Portable module ZIP creation failed" }
$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $zip
Set-Content -LiteralPath "$zip.sha256" `
    -Value ($hash.Hash.ToLowerInvariant() + "  " + (Split-Path -Leaf $zip)) -Encoding ascii
Remove-Item -LiteralPath $staging -Recurse -Force

Write-Output "Created $zip"
Write-Output "SHA-256 $($hash.Hash.ToLowerInvariant())"
