[CmdletBinding()]
param(
    [string]$InputPath = "out\device\libcameraservice.oplus.so",
    [string]$OutputPath = "out\device\libcameraservice.vcam.so"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$inputFile = [IO.Path]::GetFullPath((Join-Path $repoRoot $InputPath))
$outputFile = [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputPath))
$expectedSha256 = "2108be5d63b385282d844f689e9f34740026072b8ef6daca2ed59b23612870af"
$branchOffset = 0x1aa9f8
$expectedInstruction = [byte[]](0x21, 0x08, 0x00, 0x54) # b.ne 0x1aaafc
$replacementInstruction = [byte[]](0x1f, 0x20, 0x03, 0xd5) # nop

if (-not (Test-Path -LiteralPath $inputFile)) {
    throw "Input library not found: $inputFile"
}
$actualSha256 = (Get-FileHash -LiteralPath $inputFile -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualSha256 -ne $expectedSha256) {
    throw "Unsupported libcameraservice.so hash: $actualSha256"
}

$bytes = [IO.File]::ReadAllBytes($inputFile)
for ($index = 0; $index -lt $expectedInstruction.Length; ++$index) {
    if ($bytes[$branchOffset + $index] -ne $expectedInstruction[$index]) {
        throw ("Instruction mismatch at file offset 0x{0:x}" -f ($branchOffset + $index))
    }
    $bytes[$branchOffset + $index] = $replacementInstruction[$index]
}

$outputDirectory = Split-Path -Parent $outputFile
[IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
[IO.File]::WriteAllBytes($outputFile, $bytes)
$patchedSha256 = (Get-FileHash -LiteralPath $outputFile -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "Patched: $outputFile"
Write-Host "SHA256: $patchedSha256"
