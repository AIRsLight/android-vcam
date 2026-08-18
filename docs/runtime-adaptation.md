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

The current `libvcam_cameraserver_agent.so` exposes only
`vcam_cameraserver_agent_validate()`. It has no constructor side effects and no
activation or patching entry point. Loading it alone cannot alter camera traffic.

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
must not silently fall back to byte copying. Installation will remain disabled
until thread coordination, instruction-cache synchronization, rollback and a
pass-through Binder bridge are independently tested.

Binder transaction numbers in a strategy are part of the allowlisted build
recipe. Android 14 initial-release AIDL currently supplies the NX769J candidate
mapping, but that mapping is not considered device-qualified until the OEM
`libcamera_client.so` or read-only Binder probes confirm it.

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
