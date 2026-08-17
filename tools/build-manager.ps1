[CmdletBinding()]
param(
    [string]$AndroidSdk = "D:\AndroidSdk",
    [string]$BuildToolsVersion = "35.0.0",
    [int]$CompileSdk = 35,
    [string]$JdkHome = "$env:LOCALAPPDATA\Programs\Microsoft\jdk-17.0.10.7-hotspot",
    [string]$AppDirectory = "manager",
    [string]$BuildDirectory = "out\manager",
    [string]$OutputFile = "android-vcam-manager-debug.apk"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$appRoot = Join-Path $repoRoot $AppDirectory
$buildRoot = Join-Path $repoRoot $BuildDirectory
$toolsRoot = Join-Path $AndroidSdk "build-tools\$BuildToolsVersion"
$androidJar = Join-Path $AndroidSdk "platforms\android-$CompileSdk\android.jar"
$aapt2 = Join-Path $toolsRoot "aapt2.exe"
$d8 = Join-Path $toolsRoot "d8.bat"
$zipalign = Join-Path $toolsRoot "zipalign.exe"
$apksigner = Join-Path $toolsRoot "apksigner.bat"
$javac = Join-Path $JdkHome "bin\javac.exe"
$jar = Join-Path $JdkHome "bin\jar.exe"
$keytool = Join-Path $JdkHome "bin\keytool.exe"

foreach ($required in @($androidJar, $aapt2, $d8, $zipalign, $apksigner, $javac, $jar, $keytool)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required build file not found: $required" }
}

$env:JAVA_HOME = $JdkHome
$env:PATH = (Join-Path $JdkHome "bin") + [IO.Path]::PathSeparator + $env:PATH

New-Item -ItemType Directory -Force $buildRoot | Out-Null
$compiled = Join-Path $buildRoot "resources.zip"
$unsigned = Join-Path $buildRoot "manager-unsigned.apk"
$aligned = Join-Path $buildRoot "manager-aligned.apk"
$signed = Join-Path $buildRoot $OutputFile
$gen = Join-Path $buildRoot "generated"
$classes = Join-Path $buildRoot "classes"
$dex = Join-Path $buildRoot "dex"
$classesJar = Join-Path $buildRoot "classes.jar"
$keystore = Join-Path $buildRoot "debug.keystore"

foreach ($dir in @($gen, $classes, $dex)) {
    New-Item -ItemType Directory -Force $dir | Out-Null
}

& $aapt2 compile --dir (Join-Path $appRoot "res") -o $compiled
if ($LASTEXITCODE -ne 0) { throw "aapt2 compile failed" }

& $aapt2 link -o $unsigned -I $androidJar `
    --manifest (Join-Path $appRoot "AndroidManifest.xml") `
    --java $gen --min-sdk-version 31 --target-sdk-version 35 `
    --version-code 9 --version-name "0.3.6-dev" $compiled
if ($LASTEXITCODE -ne 0) { throw "aapt2 link failed" }

$sources = @(
    Get-ChildItem -LiteralPath (Join-Path $appRoot "src") -Recurse -Filter "*.java" | ForEach-Object FullName
    Get-ChildItem -LiteralPath $gen -Recurse -Filter "*.java" | ForEach-Object FullName
)
& $javac -encoding UTF-8 --release 8 -classpath $androidJar -d $classes @sources
if ($LASTEXITCODE -ne 0) { throw "javac failed" }

& $jar --create --file $classesJar -C $classes .
if ($LASTEXITCODE -ne 0) { throw "jar failed" }
& $d8 --lib $androidJar --min-api 31 --output $dex $classesJar
if ($LASTEXITCODE -ne 0) { throw "d8 failed" }
& $jar --update --file $unsigned -C $dex classes.dex
if ($LASTEXITCODE -ne 0) { throw "Unable to add classes.dex to APK" }

& $zipalign -f 4 $unsigned $aligned
if ($LASTEXITCODE -ne 0) { throw "zipalign failed" }

if (-not (Test-Path -LiteralPath $keystore)) {
    & $keytool -genkeypair -keystore $keystore -storepass android -keypass android `
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 `
        -dname "CN=Android Debug,O=Android,C=US"
    if ($LASTEXITCODE -ne 0) { throw "Unable to create debug keystore" }
}

& $apksigner sign --ks $keystore --ks-pass pass:android --key-pass pass:android `
    --out $signed $aligned
if ($LASTEXITCODE -ne 0) { throw "APK signing failed" }
& $apksigner verify --verbose $signed
if ($LASTEXITCODE -ne 0) { throw "APK verification failed" }

Write-Host "Built: $signed"
