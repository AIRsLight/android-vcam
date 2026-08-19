[CmdletBinding()]
param(
    [string]$AndroidSdk = "D:\AndroidSdk",
    [string]$Binary = "out\runtime-arm64-static\android_signal_quiescence_backend_test",
    [string]$NdkVersion = "27.2.12479018"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$toolchain = Join-Path $AndroidSdk "ndk\$NdkVersion\toolchains\llvm\prebuilt\windows-x86_64\bin"
$nm = Join-Path $toolchain "llvm-nm.exe"
$objdump = Join-Path $toolchain "llvm-objdump.exe"
$binaryPath = Join-Path $repoRoot $Binary

foreach ($required in @($nm, $objdump, $binaryPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required handler-audit input is missing: $required"
    }
}

$symbols = & $nm -a $binaryPath
if ($LASTEXITCODE -ne 0) {
    throw "Unable to read symbols from the ARM64 test binary"
}
$symbolLine = $symbols | Where-Object { $_ -match "parkSignalHandler" } |
    Select-Object -First 1
if (-not $symbolLine) {
    throw "Unable to locate the ARM64 signal handler symbol"
}
$symbol = ($symbolLine -split "\s+")[-1]
$disassembly = (& $objdump -d "--disassemble-symbols=$symbol" $binaryPath) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Unable to disassemble the ARM64 signal handler"
}

foreach ($requiredInstruction in @("bti", "ldaxr", "stlxr", "svc")) {
    if ($disassembly -notmatch "\b$requiredInstruction\b") {
        throw "Signal handler audit is missing instruction: $requiredInstruction"
    }
}
if ($disassembly -match "(?m)^\s*[0-9a-f]+:.*\bbl\b" -or
    $disassembly -match "@plt" -or
    $disassembly -match "__aarch64_") {
    throw "Signal handler contains a function call or outline-atomic helper"
}

Write-Output "ARM64 signal handler audit passed: inline atomics and raw SVC only"
