[CmdletBinding()]
param(
    [string]$CiHost = "ci@192.168.130.205",

    [ValidateRange(1, 65535)]
    [int]$LocalProxyPort = 1085,

    [ValidateRange(1, 65535)]
    [int]$RemoteProxyPort = 1085
)

$ErrorActionPreference = "Stop"

$ssh = Get-Command ssh.exe -ErrorAction Stop
$forward = "127.0.0.1:${RemoteProxyPort}:127.0.0.1:${LocalProxyPort}"
$arguments = @(
    "-o", "BatchMode=yes",
    "-o", "ExitOnForwardFailure=yes",
    "-o", "ServerAliveInterval=30",
    "-o", "ServerAliveCountMax=3",
    "-N",
    "-R", $forward,
    $CiHost
)

$startParameters = @{
    FilePath = $ssh.Source
    ArgumentList = $arguments
    WindowStyle = "Hidden"
    PassThru = $true
}
$process = Start-Process @startParameters

Start-Sleep -Milliseconds 750
$process.Refresh()
if ($process.HasExited) {
    throw "Unable to establish the CI reverse proxy tunnel (ssh exit code $($process.ExitCode))."
}

$probe = "curl -fsSI --max-time 15 --proxy http://127.0.0.1:$RemoteProxyPort " +
    "https://android.googlesource.com/platform/manifest >/dev/null"
& $ssh.Source -o BatchMode=yes $CiHost $probe
if ($LASTEXITCODE -ne 0) {
    Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
    throw "The tunnel started, but the remote Android source probe failed."
}

[pscustomobject]@{
    ProcessId = $process.Id
    CiHost = $CiHost
    LocalProxy = "127.0.0.1:$LocalProxyPort"
    RemoteProxy = "127.0.0.1:$RemoteProxyPort"
}
