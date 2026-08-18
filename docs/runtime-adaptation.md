# Runtime adaptation guard

Android 10-14 and OEM camera-service forks cannot safely share one private C++
layout. The runtime path therefore uses introspection only to select an exact,
reviewed ABI recipe. It does not infer class layouts, vtables, offsets or calling
conventions at run time.

## Fail-closed boundary

Before any future cameraserver interception may be activated, the agent checks
the already loaded `libcameraservice.so` against all of the following:

1. module suffix and on-disk file size;
2. GNU Build ID read from the loaded ELF program headers;
3. SHA-256 of the loaded module's backing file;
4. presence of every required exported dynamic symbol;
5. that each resolved address belongs to an executable `PT_LOAD` segment; and
6. an exact machine-code prefix for every required symbol.

Any missing or mismatched field rejects activation. A matching Android release,
SDK level, device model or symbol name alone is not sufficient. Recipes are
immutable build identities, not broad device-family rules.

The current `libvcam_cameraserver_agent.so` exposes validation and planning
entry points. `vcam_cameraserver_agent_plan()` produces an internal byte plan
against the pass-through bridge, but neither entry point changes page
permissions, allocates executable memory or writes code. The library has no
constructor side effects or activation entry point, so loading it alone cannot
alter camera traffic.

## Recipe format

Recipes are UTF-8 tab-separated text. Schema 1 identifies a binary only;
schema 2 additionally binds one reviewed runtime strategy:

```text
schema<TAB>2
architecture<TAB>arm64
module<TAB>libcameraservice.so
file_size<TAB>3132936
sha256<TAB>...
build_id<TAB>...
dependency<TAB>libcamera_client.so<TAB>size<TAB>sha256<TAB>build_id
symbol<TAB>exact_mangled_name<TAB>machine_code_prefix_hex
hook<TAB>on_transact<TAB>exact_mangled_name
transaction<TAB>connect_device<TAB>4
```

Generate a recipe from an inspected, read-only device library with:

```powershell
python tools/generate-runtime-abi-recipe.py libcameraservice.so `
  --llvm-nm D:\AndroidSdk\ndk\27.2.12479018\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-nm.exe `
  --module libcameraservice.so `
  --symbol '<exact dynamic symbol>' `
  --output runtime\recipes\device-build.tsv
```

The generator supports ELF64 little-endian binaries and accepts only exported
code symbols. A recipe should contain at least two independent boundary symbols
for an OEM CameraService build. Strategy recipes require an architecture, at
least one hook role and explicit Binder transaction roles. Generated recipes
must be reviewed and committed; they must never be generated automatically on
an end-user device and immediately trusted.

## ARM64 planning gate

`Arm64PatchPlanner` produces an entry patch and trampoline as byte arrays; it
does not call `mprotect`, allocate executable memory, suspend threads or write to
the target. The current plan overwrites 16 bytes with an absolute branch through
the ABI scratch register `x17`. The trampoline begins with `BTI C`, executes the
four original instructions (including `PACIASP` on the NX769J build), then
branches to the first untouched instruction.

Planning is rejected if any stolen instruction is PC-relative or performs
control flow. Supporting such an instruction requires an explicit relocator and
must not silently fall back to byte copying. Installation remains disabled until
thread coordination, instruction-cache synchronization and rollback are
independently tested.

## Offline patch transaction

The ARM64 plan now carries the exact 16 original target bytes separately from
the trampoline. A transactional installer validates the complete planner shape,
non-overlapping target/trampoline ranges, BTI landing pad, absolute-branch
instructions, resume address and copied original bytes before reading memory.

The installer has `empty`, `prepared`, `committed`, `rolled_back` and `failed`
states. `prepare()` only snapshots and compares the target. `commit()` requires
an injected backend to acquire an exclusive execution window, recheck the target
to close the prepare/commit race, write and synchronize the trampoline, publish
the original trampoline, then write and synchronize the entry patch. A failed
or potentially partial entry write, or a failed entry cache synchronization,
triggers an immediate original-byte restore attempt before leaving the exclusive
window. Explicit rollback first verifies that the target still contains this
transaction's entry patch so it cannot overwrite an unrelated later change.
The revalidation buffer is allocated during `prepare()` and result messages are
static, so `commit()` and `rollback()` do not allocate inside the exclusive
window.

There is deliberately no production backend. The repository provides no page-
permission changer, executable allocator, thread suspender, process-memory
writer or device activation entry point. Tests inject isolated byte buffers and
representative partial-write, cache, coordination and rollback faults; passing
them validates ordering and state handling, not safe live-process installation.

## Pass-through bridge

The Binder bridge remains policy-free. Its opaque native signature matches
`CameraService::onTransact` and forwards `this`, transaction code, both Parcel
pointers, flags and the exact `status_t` result to the original trampoline.
Original and observer binding are separate, one-shot atomic publications; an
unbound original returns `-ENOSYS`.

On ARM64 the optimized bridge is a `BTI C` landing pad followed by an acquire
load. Without an observer it still tail-branches directly to the trampoline.
With an observer, the generated path saves all five original arguments, calls
the read-only callback, restores the arguments, authenticates the return address
and tail-branches to the trampoline. The observer receives only the transaction
code, const data-Parcel pointer and its bridge-lifetime context; it cannot
replace the original result through the bridge API.

The Android 14 shadow adapter records only atomic totals for observed, ignored,
rejected and unsupported transactions. It does not persist package names,
camera IDs, UIDs or PIDs. Tests call the real global bridge entry and
prove that the observer runs first, the original receives an unchanged Parcel
cursor, and the original `status_t` is returned exactly. There is still no code
path that installs the planned entry patch or binds executable trampoline
memory.

Binder transaction numbers in a strategy are part of the allowlisted build
recipe. An AOSP AIDL layout is only a candidate until the OEM
`libcamera_client.so` or read-only Binder probes confirm it. The NX769J mapping
has been confirmed directly from its OEM `BpCameraService` machine code.

For qualified recipes, the library that implements the Binder client/server
stubs is pinned as a dependency with its own size, SHA-256 and GNU Build ID. The
guard validates that exact loaded dependency before resolving hook symbols. This
prevents an OTA from retaining `libcameraservice.so` while silently changing the
transaction layout in `libcamera_client.so`.

## Read-only Android 14 Parcel observation

The Android 14 platform adapter can now classify allowlisted transaction codes
from the ABI recipe and inspect the leading routing fields of a real AOSP
`android::Parcel`. It validates the `SYST` Binder header and the exact
`android.hardware.ICameraService` descriptor before reading anything else. The
implemented payload shapes cover Camera1 and Camera2 connects, string and
integer camera IDs, listeners, and concurrent-camera enumeration. Concurrent
session configurations remain explicitly unsupported until their nested typed
objects have a version-pinned decoder.

Observation is deliberately non-mutating. An RAII guard restores the original
`dataPosition()` on every return path, including malformed input and unsupported
payloads. The adapter does not call `enforceInterface()`, because that routine
also changes `IPCThreadState` work-source state; it performs a local descriptor
comparison instead. Observed strings are length-bounded and ASCII-only, while
the authoritative caller UID and PID come from `IPCThreadState`, never from
client-supplied Parcel fields.

The observer now has a tested adapter for the pass-through bridge, but the
cameraserver agent does not instantiate or bind it and the planned entry patch
is still never installed. It cannot select a route, replace a camera, mutate a
request, bind a trampoline, or install a hook. Its current purpose is to
establish and test the exact read-only parsing boundary before activation code
exists.

## Planned activation architecture

The compatible path is deliberately split into three layers:

- device profiler: collects fingerprint, provider transport/version and ELF
  identity without mutation;
- ABI guard: selects and validates an allowlisted recipe;
- cameraserver agent: applies a recipe-specific Binder interception strategy and
  delegates routed streams to the common virtual-camera backend.

Only the third layer is version-sensitive. Provider/media decoding, routing and
manager configuration remain shared. If no exact recipe exists, physical camera
behavior remains unchanged and the manager should report the unsupported build.

Java reflection is not part of this design: the relevant interfaces and objects
are native Binder/C++ components. `dlsym`, ELF program-header inspection and
Binder interface-version discovery provide the useful runtime introspection, but
they do not make private native layouts reflective.
