[CmdletBinding()]
param(
    [string]$SdkRoot = "D:\AndroidSdk",
    [string]$AvdName = "vcam_aosp14_ksu",
    [ValidateRange(5554, 5682)]
    [int]$Port = 5556,
    [string]$Kernel = "out/avd-kernels/kernel-ranchu-kernelsu-v3.3.0-android14-6.1-x86_64",
    [switch]$ShowKernel,
    [switch]$WipeData
)

$ErrorActionPreference = "Stop"
$sourceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$emulator = Join-Path $SdkRoot "emulator/emulator.exe"
$kernelPath = if ([System.IO.Path]::IsPathRooted($Kernel)) {
    $Kernel
} else {
    Join-Path $sourceRoot $Kernel
}

foreach ($path in @($emulator, $kernelPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file is missing: $path"
    }
}
if ($Port % 2 -ne 0) {
    throw "The emulator console port must be even"
}

$arguments = @(
    "-avd", $AvdName,
    "-port", $Port,
    "-kernel", $kernelPath,
    "-no-snapshot",
    "-no-window",
    "-gpu", "swiftshader_indirect",
    "-no-audio",
    "-no-boot-anim"
)
if ($ShowKernel) { $arguments += "-show-kernel" }
if ($WipeData) { $arguments += "-wipe-data" }

$logDirectory = Join-Path $sourceRoot "out/avd-kernels"
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$stdout = Join-Path $logDirectory "$AvdName.stdout.log"
$stderr = Join-Path $logDirectory "$AvdName.stderr.log"

$process = Start-Process -FilePath $emulator -ArgumentList $arguments `
    -WindowStyle Hidden -RedirectStandardOutput $stdout `
    -RedirectStandardError $stderr -PassThru

Write-Output "Started $AvdName on emulator-$Port (PID $($process.Id))"
Write-Output "Kernel: $kernelPath"
Write-Output "Logs: $stdout"
