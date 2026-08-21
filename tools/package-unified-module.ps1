[CmdletBinding()]
param(
    [string]$Version = "0.5.0-dev.32",
    [string]$CameraHal = "out\device\camera.qcom.vcam-proxy.so",
    [string]$AidlArtifactRoot = "out/android14-provider-probe",
    [string]$NativeArtifactRoot = "out/native/arm64-v8a",
    [string]$RouterLauncher = "out\aosp14-router\vcam_cameraserver_launcher",
    [string]$RouterLibrary = "out\aosp14-router\libvcam_cameraserver_router.so",
    [string]$OutputDirectory = "dist",
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outRoot = Join-Path $repoRoot "out"
$workRoot = Join-Path $outRoot "unified-module-package"
$profileZipRoot = Join-Path $workRoot "profile-zips"
$extractRoot = Join-Path $workRoot "extract"
$stagingRoot = Join-Path $workRoot "staging"
$outputRoot = Join-Path $repoRoot $OutputDirectory

function Reset-WorkDirectory([string]$Path) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullOut = [IO.Path]::GetFullPath($outRoot)
    if (-not $fullPath.StartsWith(
            $fullOut + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace work directory outside out: $fullPath"
    }
    if (Test-Path -LiteralPath $fullPath) {
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $fullPath | Out-Null
}

function Copy-DirectoryContents([string]$Source, [string]$Destination) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

function Remove-ProfileMetadata([string]$ProfileRoot) {
    foreach ($name in @("module.prop", "README.md", "skip_mount")) {
        $path = Join-Path $ProfileRoot $name
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
}

Reset-WorkDirectory $workRoot
New-Item -ItemType Directory -Force -Path $profileZipRoot, $extractRoot | Out-Null

$relativeProfileZipRoot = [IO.Path]::GetRelativePath($repoRoot, $profileZipRoot)
& (Join-Path $PSScriptRoot "package-apmodule.ps1") `
    -CameraHal $CameraHal `
    -OutputDirectory $relativeProfileZipRoot `
    -Python $Python
if ($LASTEXITCODE -ne 0) { throw "OnePlus profile packaging failed" }

$aidlZip = Join-Path $profileZipRoot "nx-aidl-provider.zip"
& (Join-Path $PSScriptRoot "package-aosp14-aidl-provider.ps1") `
    -ArtifactRoot $AidlArtifactRoot `
    -NativeArtifactRoot $NativeArtifactRoot `
    -Output ([IO.Path]::GetRelativePath($repoRoot, $aidlZip)) `
    -Python $Python
if ($LASTEXITCODE -ne 0) { throw "NX769J provider profile packaging failed" }

& (Join-Path $PSScriptRoot "package-portable-bootstrap.ps1") `
    -Launcher $RouterLauncher `
    -Router $RouterLibrary `
    -OutputDirectory $relativeProfileZipRoot `
    -BootstrapMode physical-route `
    -Python $Python
if ($LASTEXITCODE -ne 0) { throw "NX769J router profile packaging failed" }

$oneplusZip = Get-ChildItem -LiteralPath $profileZipRoot `
    -Filter "android-vcam-oneplus7pro-apm-v*.zip" | Select-Object -First 1
$routerZip = Get-ChildItem -LiteralPath $profileZipRoot `
    -Filter "android-vcam-portable-bootstrap-v*-physical-route.zip" | Select-Object -First 1
if (-not $oneplusZip -or -not $routerZip) { throw "Device profile archives are incomplete" }

$oneplusExtract = Join-Path $extractRoot "oneplus"
$providerExtract = Join-Path $extractRoot "nx-provider"
$routerExtract = Join-Path $extractRoot "nx-router"
Expand-Archive -LiteralPath $oneplusZip.FullName -DestinationPath $oneplusExtract
Expand-Archive -LiteralPath $aidlZip -DestinationPath $providerExtract
Expand-Archive -LiteralPath $routerZip.FullName -DestinationPath $routerExtract

Move-Item -LiteralPath (Join-Path $oneplusExtract "customize.sh") `
    -Destination (Join-Path $oneplusExtract "install-profile.sh")
Move-Item -LiteralPath (Join-Path $oneplusExtract "service.sh") `
    -Destination (Join-Path $oneplusExtract "profile-service.sh")
Remove-ProfileMetadata $oneplusExtract
$proxySource = Join-Path $oneplusExtract "vendor/lib64/libvcam_proxy.so"
$proxySlot = Join-Path $oneplusExtract "vendor/lib64/hw/local_time.default.so"
Copy-Item -LiteralPath $proxySource -Destination $proxySlot -Force
$oneplusInstaller = Join-Path $oneplusExtract "install-profile.sh"
$oneplusInstallerText = [IO.File]::ReadAllText($oneplusInstaller).Replace(
    "This module uses guarded bind mounts; no metamodule is required",
    "System files use the active MetaModule mount lifecycle")
[IO.File]::WriteAllText($oneplusInstaller, $oneplusInstallerText, [Text.UTF8Encoding]::new($false))

Move-Item -LiteralPath (Join-Path $providerExtract "customize.sh") `
    -Destination (Join-Path $providerExtract "install-provider.sh")
Move-Item -LiteralPath (Join-Path $providerExtract "service.sh") `
    -Destination (Join-Path $providerExtract "provider-service.sh")
Move-Item -LiteralPath (Join-Path $routerExtract "customize.sh") `
    -Destination (Join-Path $routerExtract "install-router.sh")
Move-Item -LiteralPath (Join-Path $routerExtract "service.sh") `
    -Destination (Join-Path $routerExtract "router-service.sh")
Remove-ProfileMetadata $providerExtract
Remove-ProfileMetadata $routerExtract

$providerPolicy = Join-Path $providerExtract "sepolicy.rule"
$routerPolicy = Join-Path $routerExtract "sepolicy.rule"
$policyLines = @()
$policyLines += Get-Content -LiteralPath $providerPolicy
$policyLines += Get-Content -LiteralPath $routerPolicy
$policyLines = $policyLines | Where-Object { $_ -ne $null } | Select-Object -Unique
[IO.File]::WriteAllText(
    $providerPolicy, (($policyLines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
Remove-Item -LiteralPath $routerPolicy -Force

Copy-DirectoryContents (Join-Path $repoRoot "unified-module") $stagingRoot
$profilesRoot = Join-Path $stagingRoot "payload/profiles"
$oneplusProfile = Join-Path $profilesRoot "oneplus7pro-p202303230244"
$nxProfile = Join-Path $profilesRoot "nx769j-ukq1-20240417"
Copy-DirectoryContents $oneplusExtract $oneplusProfile
Copy-DirectoryContents $providerExtract $nxProfile
Copy-DirectoryContents $routerExtract $nxProfile

$profileProp = Join-Path $stagingRoot "module.prop"
$profilePropText = [IO.File]::ReadAllText($profileProp)
$profilePropText = [Text.RegularExpressions.Regex]::Replace(
    $profilePropText, "(?m)^version=.*$", "version=$Version")
[IO.File]::WriteAllText($profileProp, $profilePropText, [Text.UTF8Encoding]::new($false))

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$outputPath = Join-Path $outputRoot "android-vcam-module-v$Version.zip"
if (Test-Path -LiteralPath $outputPath) { Remove-Item -LiteralPath $outputPath -Force }
& $Python (Join-Path $PSScriptRoot "create-module-zip.py") $stagingRoot $outputPath
if ($LASTEXITCODE -ne 0) { throw "Unified module ZIP creation failed" }

$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $outputPath
Set-Content -LiteralPath "$outputPath.sha256" `
    -Value ($hash.Hash.ToLowerInvariant() + "  " + (Split-Path -Leaf $outputPath)) `
    -Encoding ascii
Remove-Item -LiteralPath $workRoot -Recurse -Force

Write-Output "Created $outputPath"
Write-Output "SHA-256 $($hash.Hash.ToLowerInvariant())"
