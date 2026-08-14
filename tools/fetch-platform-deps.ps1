[CmdletBinding()]
param(
    [string]$Tag = "android-12.0.0_r34"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$referenceRoot = Join-Path $repoRoot ".reference"
New-Item -ItemType Directory -Force $referenceRoot | Out-Null

function Get-SparseAndroidRepository {
    param(
        [string]$Name,
        [string]$Url,
        [string[]]$Paths
    )

    $target = Join-Path $referenceRoot $Name
    if (Test-Path -LiteralPath (Join-Path $target ".git")) {
        Write-Host "Using existing $Name"
        return
    }
    if (Test-Path -LiteralPath $target) {
        throw "Path exists but is not a Git checkout: $target"
    }

    & git clone --depth 1 --filter=blob:none --sparse --branch $Tag $Url $target
    if ($LASTEXITCODE -ne 0) { throw "Unable to clone $Name" }
    & git -C $target sparse-checkout set @Paths
    if ($LASTEXITCODE -ne 0) { throw "Unable to configure sparse checkout for $Name" }
}

function Get-AndroidRepository {
    param(
        [string]$Name,
        [string]$Url
    )

    $target = Join-Path $referenceRoot $Name
    if (Test-Path -LiteralPath (Join-Path $target ".git")) {
        Write-Host "Using existing $Name"
        return
    }
    if (Test-Path -LiteralPath $target) {
        throw "Path exists but is not a Git checkout: $target"
    }

    & git clone --depth 1 --branch $Tag $Url $target
    if ($LASTEXITCODE -ne 0) { throw "Unable to clone $Name" }
}

Get-SparseAndroidRepository "libhardware" `
    "https://android.googlesource.com/platform/hardware/libhardware" `
    @("include")
Get-SparseAndroidRepository "system_core" `
    "https://android.googlesource.com/platform/system/core" `
    @("libcutils/include", "libsystem/include")
Get-SparseAndroidRepository "system_media" `
    "https://android.googlesource.com/platform/system/media" `
    @("camera/include")
Get-SparseAndroidRepository "frameworks_native" `
    "https://android.googlesource.com/platform/frameworks/native" `
    @("libs/nativebase/include")
Get-AndroidRepository "libjpeg_turbo" `
    "https://android.googlesource.com/platform/external/libjpeg-turbo"

Write-Host "Android $Tag platform headers are ready under $referenceRoot"
