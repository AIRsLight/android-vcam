[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
if ($Version -notmatch '^\d+\.\d+\.\d+-dev\.\d+$') {
    throw "Expected a development version such as 0.5.0-dev.43"
}
if ($Version -eq '0.5.0-dev.42') { throw "dev.42 was withdrawn; do not reuse its tag" }
$branch = git -C $RepoRoot branch --show-current
if ($LASTEXITCODE -ne 0 -or $branch -ne 'dev') {
    throw "Development releases must be built from the dev branch (found: $branch)"
}
$changes = git -C $RepoRoot status --porcelain=v1
if ($LASTEXITCODE -ne 0) { throw "Unable to read source state" }
if ($changes) { throw "Commit source changes on dev before building a release" }
$commit = git -C $RepoRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw "Unable to identify source revision" }
Write-Output $commit
