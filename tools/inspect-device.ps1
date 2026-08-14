param(
    [string]$Adb = "adb",
    [string]$OutputDirectory = "out/device-inspection"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root $OutputDirectory
New-Item -ItemType Directory -Force $output | Out-Null

function Invoke-AdbShell {
    param([string]$Command)
    & $Adb shell su -c $Command
    if ($LASTEXITCODE -ne 0) {
        throw "adb shell command failed: $Command"
    }
}

$properties = Invoke-AdbShell "getprop ro.product.device; getprop ro.build.version.sdk; getprop ro.product.cpu.abi; getprop ro.build.fingerprint; getprop ro.board.platform"
$hal = Invoke-AdbShell "sha256sum /vendor/lib64/hw/camera.qcom.so; stat -c '%a %u %g %C %s' /vendor/lib64/hw/camera.qcom.so"
$camera = Invoke-AdbShell "lshal 2>/dev/null | grep -i camera; dumpsys media.camera 2>/dev/null | head -n 120"
$kernel = Invoke-AdbShell "uname -a; getenforce; zcat /proc/config.gz 2>/dev/null | grep -E 'CONFIG_(MODULE_SIG_FORCE|MODVERSIONS|VIDEO_V4L2|USB_VIDEO_CLASS)='"

Set-Content -LiteralPath (Join-Path $output "properties.txt") -Value $properties -Encoding utf8NoBOM
Set-Content -LiteralPath (Join-Path $output "camera-hal.txt") -Value $hal -Encoding utf8NoBOM
Set-Content -LiteralPath (Join-Path $output "camera-service.txt") -Value $camera -Encoding utf8NoBOM
Set-Content -LiteralPath (Join-Path $output "kernel.txt") -Value $kernel -Encoding utf8NoBOM

Write-Output "Inspection written to $output"
