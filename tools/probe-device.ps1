param(
    [string]$Adb = "D:\AndroidSdk\platform-tools\adb.exe",
    [string]$Serial = "",
    [string]$Output = "out/device-inspection/device-profile.conf",
    [string]$EvaluationOutput = "out/device-inspection/capability-result.conf",
    [string]$Python = "python",
    [switch]$SkipEvaluation
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$probe = Join-Path $root "apmodule/device-probe.sh"
$evaluator = Join-Path $root "tools/evaluate-aosp14-capability.py"
$outputPath = Join-Path $root $Output
$evaluationPath = Join-Path $root $EvaluationOutput
$remoteProbe = "/data/local/tmp/android-vcam-device-probe.sh"
$adbTarget = @()
if ($Serial) {
    $adbTarget = @("-s", $Serial)
}

if (-not (Test-Path -LiteralPath $probe)) {
    throw "Device probe not found: $probe"
}
if (-not $SkipEvaluation -and -not (Test-Path -LiteralPath $evaluator)) {
    throw "Capability evaluator not found: $evaluator"
}

& $Adb @adbTarget get-state | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "ADB device is not connected"
}

& $Adb @adbTarget push $probe $remoteProbe | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Unable to upload device probe"
}

try {
    & $Adb @adbTarget shell chmod 0755 $remoteProbe
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to make device probe executable"
    }

    $rootIdentity = & $Adb @adbTarget shell su -c id 2>$null
    $useRoot = $LASTEXITCODE -eq 0 -and ($rootIdentity -join " ") -match "uid=0"
    if ($useRoot) {
        $profile = & $Adb @adbTarget shell su -c $remoteProbe
    } else {
        $profile = & $Adb @adbTarget shell $remoteProbe
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Device capability probe failed"
    }

    $outputDirectory = Split-Path -Parent $outputPath
    New-Item -ItemType Directory -Force $outputDirectory | Out-Null
    Set-Content -LiteralPath $outputPath -Value $profile -Encoding utf8NoBOM
    $profile
    Write-Output "Profile written to $outputPath"

    if (-not $SkipEvaluation) {
        $evaluationDirectory = Split-Path -Parent $evaluationPath
        New-Item -ItemType Directory -Force $evaluationDirectory | Out-Null
        $arguments = @($evaluator, $outputPath)
        foreach ($evidence in @(
            @("--router-stats", "/dev/vcam/router.stats", "runtime-router.stats"),
            @("--topology", "/data/vendor/camera/vcam/topology.conf", "runtime-topology.conf")
        )) {
            if ($useRoot) {
                $content = & $Adb @adbTarget shell su -c "cat $($evidence[1])" 2>$null
            } else {
                $content = & $Adb @adbTarget shell cat $evidence[1] 2>$null
            }
            if ($LASTEXITCODE -eq 0 -and $null -ne $content -and $content.Count -gt 0) {
                $localEvidence = Join-Path $evaluationDirectory $evidence[2]
                Set-Content -LiteralPath $localEvidence -Value $content -Encoding utf8NoBOM
                $arguments += @($evidence[0], $localEvidence)
            }
        }
        $arguments += @("--output", $evaluationPath)
        & $Python @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Capability evaluation failed"
        }
        Write-Output "Evaluation written to $evaluationPath"
    }
} finally {
    & $Adb @adbTarget shell rm -f $remoteProbe 2>$null
}
