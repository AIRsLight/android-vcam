[CmdletBinding()]
param(
    [string]$AndroidSdk = "D:\AndroidSdk",
    [string]$BuildToolsVersion = "35.0.0",
    [int]$CompileSdk = 35,
    [int]$MinSdk = 29,
    [string]$JdkHome = "$env:LOCALAPPDATA\Programs\Microsoft\jdk-17.0.10.7-hotspot",
    [string]$Output = "out\backend-java\vcam-https-downloader.jar"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot "out\backend-java\build"
$outputPath = Join-Path $repoRoot $Output
$androidJar = Join-Path $AndroidSdk "platforms\android-$CompileSdk\android.jar"
$d8 = Join-Path $AndroidSdk "build-tools\$BuildToolsVersion\d8.bat"
$javac = Join-Path $JdkHome "bin\javac.exe"
$jar = Join-Path $JdkHome "bin\jar.exe"

foreach ($required in @($androidJar, $d8, $javac, $jar)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required build file not found: $required"
    }
}

$classes = Join-Path $buildRoot "classes"
$dex = Join-Path $buildRoot "dex"
$classesJar = Join-Path $buildRoot "classes.jar"
foreach ($path in @($classes, $dex)) {
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    Get-ChildItem -LiteralPath $path -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outputPath) | Out-Null

$sourceRoot = Join-Path $repoRoot "backend-java\src"
$sources = @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -Filter "*.java" |
    ForEach-Object FullName)
if ($sources.Count -eq 0) { throw "No backend Java sources found under $sourceRoot" }

& $javac -encoding UTF-8 --release 8 -classpath $androidJar -d $classes @sources
if ($LASTEXITCODE -ne 0) { throw "Backend Java compilation failed" }
& $jar --create --file $classesJar -C $classes .
if ($LASTEXITCODE -ne 0) { throw "Backend classes archive failed" }
& $d8 --lib $androidJar --min-api $MinSdk --output $dex $classesJar
if ($LASTEXITCODE -ne 0) { throw "Backend dex compilation failed" }
if (Test-Path -LiteralPath $outputPath) { Remove-Item -LiteralPath $outputPath -Force }
& $jar --create --file $outputPath -C $dex classes.dex
if ($LASTEXITCODE -ne 0) { throw "Backend dex archive failed" }

$result = Get-Item -LiteralPath $outputPath
$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $outputPath
Write-Output "Built $($result.FullName)"
Write-Output "Size: $($result.Length) bytes"
Write-Output "SHA-256: $($hash.Hash)"
