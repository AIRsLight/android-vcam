# Workspace conventions

- Use PowerShell 7 (`pwsh`) for shell work on Windows.
- Active development and development releases belong on `dev`.
- Build development releases from a clean, committed `dev` checkout. Publish
  `v<version>-dev.<number>` as a GitHub pre-release targeting that exact commit.
- Use the normal release assets: one auto-selecting root module ZIP, manager
  APK, test APK and checksums/manifest. Engineering testkit archives are local
  diagnostics, not a separate public release channel.
- Merge the tested revision into `main` only after the user confirms testing
  passed or explicitly authorizes promotion. Issue reports alone do not
  automatically authorize a merge or establish device qualification.
- Never reuse the withdrawn `v0.5.0-dev.42` tag. Start subsequent releases at
  dev.43 or later. Do not rewrite the existing `main` history during this split.

See [release workflow](docs/release-workflow.md).
