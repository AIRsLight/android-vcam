[CmdletBinding()]
param(
    [string]$AndroidSdk = "D:\AndroidSdk",
    [string]$Publisher = "out\native\arm64-v8a\vcam-publisher"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$adb = Join-Path $AndroidSdk "platform-tools\adb.exe"
$publisherPath = $Publisher
if (-not [IO.Path]::IsPathRooted($publisherPath)) {
    $publisherPath = Join-Path $repoRoot $publisherPath
}
if (-not (Test-Path -LiteralPath $adb)) { throw "adb not found: $adb" }
if (-not (Test-Path -LiteralPath $publisherPath)) {
    throw "Publisher not found: $publisherPath"
}

$existing = (& $adb shell "su -c 'if [ -e /data/vendor/camera/vcam ]; then echo exists; else echo absent; fi'").Trim()
if ($existing -ne "absent") {
    throw "Refusing to overwrite existing /data/vendor/camera/vcam"
}

& $adb push $publisherPath /data/local/tmp/vcam-publisher
if ($LASTEXITCODE -ne 0) { throw "Unable to push publisher" }

try {
    $test = 'mkdir -p /data/vendor/camera/vcam; chown camera:camera /data/vendor/camera/vcam; chmod 0770 /data/vendor/camera/vcam; restorecon -RF /data/vendor/camera/vcam; chmod 0755 /data/local/tmp/vcam-publisher; printf "VCAMRGB1\001\000\000\000\001\000\000\000\003\000\000\000\001\000\000\000\377\000\000" | /data/local/tmp/vcam-publisher; result=$?; echo publisher_exit=$result; ls -ldZ /data/vendor/camera/vcam; ls -lZ /data/vendor/camera/vcam/source.rgb; wc -c /data/vendor/camera/vcam/source.rgb; exit $result'
    & $adb shell "su -c '$test'"
    if ($LASTEXITCODE -ne 0) { throw "Frame publisher device test failed" }
} finally {
    $cleanup = 'rm -f /data/local/tmp/vcam-publisher /data/vendor/camera/vcam/source.rgb /data/vendor/camera/vcam/source.rgb.new; rmdir /data/vendor/camera/vcam'
    & $adb shell "su -c '$cleanup'" | Out-Null
}

$remaining = (& $adb shell "su -c 'if [ -e /data/vendor/camera/vcam ] || [ -e /data/local/tmp/vcam-publisher ]; then echo leftover; else echo clean; fi'").Trim()
if ($remaining -ne "clean") { throw "Temporary publisher test files were not removed" }
Write-Host "Publisher protocol and SELinux label test passed; temporary files removed"
