[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$NotesFile,
    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$commit = & (Join-Path $PSScriptRoot "assert-dev-release-source.ps1") -Version $Version
$manifestPath = Join-Path $repoRoot "dist/android-vcam-supported-v$Version.json"
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.release -ne $Version -or $manifest.source_branch -ne 'dev' -or
    $manifest.source_commit -ne $commit) {
    throw "Release manifest must match the current committed dev source"
}
if (-not (Test-Path -LiteralPath $NotesFile -PathType Leaf)) { throw "Release notes missing: $NotesFile" }
$expectedNames = @(
    "android-vcam-module-v$Version.zip",
    "android-vcam-manager-v$Version-debug.apk",
    "android-vcam-camera2-test-v$Version-debug.apk"
)
if ($manifest.artifacts.Count -ne 3 -or
    (Compare-Object ($manifest.artifacts.file | Sort-Object) ($expectedNames | Sort-Object))) {
    throw "Publish exactly one normal module ZIP and the two APKs"
}
$assets = @($manifestPath)
foreach ($artifact in $manifest.artifacts) {
    $path = Join-Path $repoRoot "dist/$($artifact.file)"
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $artifact.sha256 -or (Get-Item -LiteralPath $path).Length -ne $artifact.bytes) {
        throw "Artifact changed after manifest generation: $($artifact.file)"
    }
    $assets += $path
}
if ($ValidateOnly) {
    Write-Output "Validated normal dev release $Version at $commit; nothing published"
    return
}
$remote = git -C $repoRoot ls-remote origin refs/heads/dev
if ($LASTEXITCODE -ne 0 -or -not $remote -or ($remote -split '\s+')[0] -ne $commit) {
    throw "Push this dev commit before publishing"
}
$tag = "v$Version"
$existingTag = git -C $repoRoot ls-remote origin "refs/tags/$tag"
if ($LASTEXITCODE -ne 0) { throw "Unable to inspect remote tags" }
if ($existingTag) { throw "Tag already exists; use a new development version: $tag" }
gh release create $tag @assets --repo AIRsLight/android-vcam --target $commit `
    --title "VCAM $Version" --notes-file $NotesFile --prerelease --latest=false
if ($LASTEXITCODE -ne 0) { throw "Development release publication failed" }
