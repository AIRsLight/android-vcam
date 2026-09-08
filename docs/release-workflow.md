# Release workflow

Effective 2026-09-07, development and dev-release builds use `dev`. `main`
receives a tested revision only after test results are reviewed and the user
authorizes promotion. This policy does not retroactively certify the source
already merged into main before the branch split.

## Development releases

1. Implement, run applicable build/offline checks and commit on `dev`.
2. Build with an explicit unused development version:

   ```powershell
   pwsh -File tools/package-supported-release.ps1 -Version 0.5.0-dev.44 -VersionCode 64
   ```

3. Push the exact source commit to `origin/dev`.
4. Publish the normal module ZIP, manager APK, test APK and source/hash manifest:

   ```powershell
   pwsh -File tools/publish-dev-release.ps1 -Version 0.5.0-dev.44 -NotesFile docs/releases/supported-dev44.md -ValidateOnly
   pwsh -File tools/publish-dev-release.ps1 -Version 0.5.0-dev.44 -NotesFile docs/releases/supported-dev44.md
   ```

The build and manifest entry points reject main, detached HEAD and dirty source
trees. Publishing also verifies the exact remote dev commit, artifact hashes,
normal asset names and an unused tag. GitHub dev releases are pre-releases and
are not promoted to Latest. There is no automatic merge from dev to main.

The one-module installer continues to advertise only the profiles it actually
contains. A new experimental adapter must first be integrated into that normal
installer before it can be included in a normal dev release. A branch name is
not a device qualification result.

## Promotion

Collect failures and compatibility results in Issues against the dev tag,
device and ROM. Record untested cases explicitly. Once testing passes and
promotion is authorized, merge the tested commit into main and push main.
If dev advanced after testing, promote only the reviewed commit, not the newer
untested tip. Do not move release tags or reuse version numbers.

## Withdrawn release

The separately packaged OnePlus community test release `v0.5.0-dev.42` was
removed from GitHub at the user's request. Its uploaded assets were removed
with the release. Local artifacts and the source tag are retained for audit;
the version remains reserved. dev.39 is unchanged. Future public testing uses
ordinary dev releases from `dev`, not an outer community-testkit ZIP.

The old `package-oneplus-testkit.ps1` remains available only as a local diagnostic
helper. It is not the public release entry point.
