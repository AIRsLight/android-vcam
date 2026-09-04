[CmdletBinding()]
param(
    [string]$SdkRoot = "D:\AndroidSdk",
    [string]$Serial = "emulator-5556",
    [string]$Ksud = "/data/adb/ksud",
    [string]$ModuleZip,
    [switch]$RunBootEvents
)

$ErrorActionPreference = "Stop"
$sourceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$adb = Join-Path $SdkRoot "platform-tools/adb.exe"
if (-not (Test-Path -LiteralPath $adb -PathType Leaf)) {
    throw "ADB is missing: $adb"
}

function Invoke-AdbShell {
    param([Parameter(Mandatory)][string]$Command)
    & $adb -s $Serial shell $Command
    if ($LASTEXITCODE -ne 0) {
        throw "ADB shell command failed: $Command"
    }
}

& $adb -s $Serial wait-for-device
if ($LASTEXITCODE -ne 0) { throw "Device is unavailable: $Serial" }
& $adb -s $Serial root | Out-Host
if ($LASTEXITCODE -ne 0) { throw "ADB root failed: $Serial" }
Start-Sleep -Seconds 2

$sdk = (& $adb -s $Serial shell getprop ro.build.version.sdk).Trim()
$abi = (& $adb -s $Serial shell getprop ro.product.cpu.abi).Trim()
if ($sdk -ne "34" -or $abi -ne "x86_64") {
    throw "Expected Android 14 x86_64, found sdk=$sdk abi=$abi"
}

Invoke-AdbShell "test -x $Ksud"
$kernelVersion = (& $adb -s $Serial shell "$Ksud debug version").Trim()
if ($kernelVersion -notmatch "Kernel Version:\s*[1-9][0-9]*") {
    throw "KernelSU kernel interface is unavailable: $kernelVersion"
}
$config = (& $adb -s $Serial shell "zcat /proc/config.gz 2>/dev/null | grep '^CONFIG_KSU=y$'").Trim()
if ($config -ne "CONFIG_KSU=y") {
    throw "Running kernel does not expose CONFIG_KSU=y"
}

if ($ModuleZip) {
    $modulePath = if ([System.IO.Path]::IsPathRooted($ModuleZip)) {
        $ModuleZip
    } else {
        Join-Path $sourceRoot $ModuleZip
    }
    $modulePath = (Resolve-Path -LiteralPath $modulePath).Path
    $remoteModule = "/data/local/tmp/" + [System.IO.Path]::GetFileName($modulePath)
    & $adb -s $Serial push $modulePath $remoteModule | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Unable to push module archive" }
    Invoke-AdbShell "$Ksud module install $remoteModule"
}

if ($RunBootEvents) {
    # Diagnostic fallback for an unpatched kernel. The qualified harness kernel
    # initializes KernelSU after SELinux policy load and runs these events
    # automatically during a normal boot.
    Invoke-AdbShell "$Ksud post-fs-data"
    Invoke-AdbShell "$Ksud services"
    Invoke-AdbShell "$Ksud boot-completed"
}

$modules = & $adb -s $Serial shell "$Ksud module list"
if ($LASTEXITCODE -ne 0) { throw "Unable to enumerate KernelSU modules" }

Write-Output "Android: sdk=$sdk abi=$abi"
Write-Output $kernelVersion
Write-Output "Kernel config: $config"
Write-Output "Modules:"
Write-Output $modules
