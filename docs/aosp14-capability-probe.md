# Android 14 capability probe

The generic Android 14 path separates evidence collection from routing
authorization. An unknown API 34 build can become a probe candidate, but no
runtime result can promote that build by itself.

## Classification

`device-probe.sh` schema 6 emits two independent decisions:

| Field | Meaning |
| --- | --- |
| `profile_status` | Exact committed device recipe result |
| `platform_candidate_status` | Whether the Android 14 generic probe is meaningful |
| `recommended_route_scope` | `global_only` for an unknown API 34 candidate |
| `activation_policy` | `probe_only`, `exact_profile`, or `blocked` |
| `routing_authorized` | True only for a committed exact profile |

An Enforcing API 34 device with a supported 64-bit ABI, a visible
`media.camera` Binder and a discoverable camera-provider transport is classified
as `probe_required`. This is not a support claim. Missing service/transport,
Permissive SELinux or an unsupported ABI remains blocked.

## Evidence evaluator

Collect the read-only profile and evaluate it with:

```powershell
pwsh -File tools/probe-device.ps1 -Output out/device-inspection/profile.conf
```

The helper automatically writes `capability-result.conf`. If readable router
or topology evidence already exists on the connected device, it is collected
and evaluated in the same pass. `-SkipEvaluation` retains profile-only behavior.

After a separately staged pass-through qualification, the evaluator can also
consume router and topology evidence:

```powershell
python tools/evaluate-aosp14-capability.py `
  out/device-inspection/profile.conf `
  --router-stats out/device-inspection/router.stats `
  --topology out/device-inspection/topology.conf `
  --output out/device-inspection/capability-result.conf
```

The generic evaluator accepts protocol evidence only from
`pass_through_ready`. If a candidate is already in `physical_route_ready`, it is
rejected because evidence collection must not be able to grant itself camera
routing. All required protocol bits must be seen and valid, with none invalid or
unsupported. A valid topology advances the result only to
`manual_review_required`; `routing_authorized` remains false.

Global preview, reboot recovery and a reviewed immutable recipe remain manual
promotion gates. Once reviewed, the exact fingerprint and CameraService ABI are
committed through the normal device-profile path. Per-application routing is not
inferred from the generic result; if application identity cannot be proven on a
new stack, the future promoted recipe remains global-only.

## Current delivery boundary

The unified release installer still accepts only the two qualified exact
profiles. It does not install a router or provider on an unknown candidate.
Schema 6 is exposed through the existing backend capability command when a
development/profile package is present, and the root-free Manager labels an
otherwise unknown API 34 device as requiring a probe rather than as certified.
