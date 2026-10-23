# Explicit hybrid native integration

This source recipe builds one **Linux x86-64 GNU hybrid** shared library:
accepted C futures, callback payloads and opaque instances; selected authored C
labels, error scopes and drop-in routing; retained Zig compiler, native resource
objects and **real retained Vulkan backend**. It is not a whole-C runtime,
production promotion or integrated acceptance of the newer C owners.

No SDK/tool download, installation, credential, paid service or remote host is
used. Existing GCC/G++, GNU ar/make, Python 3, readelf, repository-pinned Zig
0.15.2 and an existing Vulkan loader link path are required. Outputs and compiler
caches go in a new caller-selected absolute directory, preferably `/tmp`.

## Source-only build and CPU checks

From the repository root, using existing tool locations:

```sh
python3 runtime/c/integration/build-native.py \
  --output /tmp/doe-native-c-fresh \
  --zig "$ZIG" \
  --vulkan-prefix "$VULKAN_PREFIX"
```

Omit `--vulkan-prefix` when `libvulkan.so` already exists on the system linker
search path. The prefix is an existing directory containing `lib/libvulkan.so`,
not a driver installation. The script refuses a reused output directory and a
wrong toolchain/host. It calls each owner's producer, supplies all six
`-Dexperimental-c-*-archive` options to `zig build dropin -j1`, compiles the same
public DSO consumer as C11 and C++17, and checks actual ELF linkage. Sources are
hashed before and after; drift retains logs but prevents a success receipt.

The script's CPU-only processes deliberately point both `VK_DRIVER_FILES` and
`VK_ICD_FILENAMES` at a nonexistent file. The real Vulkan adapter probe then
reports the retained `Error` status and `vulkan physical adapter probe failed`
message. This is distinct from the existing instance consumer's forced Metal
`Unavailable` callback. Neither path establishes physical Vulkan execution.

The success receipt binds selected source hashes, exact commands and actual DSO
bytes. Private storage bridges are local in the DSO: `private-bridges.map` names
only four futures and five error-scope bridge functions, not public C APIs or
WebGPU exports. ELF inspection plus hostile `LD_PRELOAD` checks cover the linked
artifact, not merely header visibility. The original bridge-publication defect
was reproduced as an abort under that hostile preload before the link fix.

## Authorized physical-host handoff, not run by this recipe

Run only on a separately authorized healthy physical host with working Vulkan.
Do not carry the deliberately invalid ICD overrides from the CPU checks. Both
selectors below are necessary: `DOE_BACKEND` chooses the retained native backend;
`FAWN_BACKEND_LANE` chooses Doe ownership instead of the default Dawn route.

```sh
DOE_BACKEND=vulkan FAWN_BACKEND_LANE=vulkan_doe_app \
  /tmp/doe-native-c-fresh/native-smoke-c \
  /tmp/doe-native-c-fresh/install/lib/libwebgpu_doe.so hardware
```

Repeat with `native-smoke-cpp` for the C++ consumer. Hardware mode requires Vulkan
adapter identity, nonzero vendor/device IDs and a non-CPU/non-unknown adapter.
It creates a device and buffers, sets a copied label, uploads a fixed pattern,
submits a real buffer copy, waits for map completion and requires exact readback.
It does not replace a compute/graphics workload, physical lifecycle qualification,
Dawn comparison or full integrated migration evaluation. Unproven asynchronous
completion exits with code 3 without explicitly freeing still-live owners.

No physical host or GPU was used during cloud authoring. Native Vulkan remains
retained Zig. Separately authored C resource/Vulkan/Metal lanes are not silently
selected by this recipe.

## Focused dependency regression

```sh
python3 runtime/c/integration/test_header_dependencies.py
```

This copies only the error-scope owner and pinned header to an isolated temporary
tree, builds the real Make targets, changes that header, and requires both
objects plus the archive to rebuild. An optional historical `--makefile` with
`--expect-stale` reproduces the prior missing-prerequisite defect. It never edits
accepted semantic tests or the real pinned header.

## Platform boundaries

- The selected producer receipts and Zig admission checks intentionally require
  native Linux x86-64 GNU. Existing guards remain, rather than pretending source
  portability qualifies another ABI.
- Futures privately contains POSIX pthread storage and a Linux-only guard. It
  uses `pthread_condattr_setclock(CLOCK_MONOTONIC)` and monotonic timed waits.
  Windows needs an explicit synchronization/time owner and layout qualification;
  macOS needs a correct equivalent timed-wait implementation, not guard removal.
- Producer receipts currently bind GNU ELF tools, target triple, native header
  layout and tool/source hashes. Mach-O/PE receipt and private-symbol mechanisms
  require their own implemented checks.
- The consumer includes a Windows `LoadLibraryA`/`GetProcAddress` branch and a
  POSIX loader branch. Linux C11/C++17 compile/run is exercised; Windows and
  macOS branches are **authored, uncompiled and unexercised**. This does not imply
  the six selected owners build on those targets.
