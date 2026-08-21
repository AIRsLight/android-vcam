# Unified root module

This template is the only root-module surface published to users. Its installer
requires the active MetaModule supplied by KernelSU or APatch, selects an exact
qualified device profile, and copies only that profile into the final
`android_vcam` module tree. Unknown fingerprints and camera ABIs fail closed.

Device-specific module archives remain build inputs only. They are expanded
under `payload/profiles` by `tools/package-unified-module.ps1` and are not
published as independent modules.
