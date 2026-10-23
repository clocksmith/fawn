# Linux hybrid hardware-smoke handoff, not qualified

Frozen source identity: 71f774150e118198a4bf202983c6dd4125ac761326e59c9065993aec5d86931a (1,744 path/hash inputs in the retained build receipt).
All recorded inputs matched the shared working tree at 2026-10-23 16:52 UTC.
Artifact SHA256: 3750e42ee7876fae959ab306182d9b21b2a3efabc2a6494fa47474dcc7796cc2.
Current artifact/consumers: /tmp/native-c-integration-20261023/reproduced.
Source recipe: runtime/c/integration/build-native.py; instructions: runtime/c/integration/README.md.

Use only root-authorized Suby isolation, never Miles. No host is accessed by the
cloud author. Before load, the physical-host owner must verify healthy cooling,
normal idle load, existing Vulkan driver/loader, and no conflicting GPU work.
Do not bypass a failed cooling/driver/authorization preflight or silently switch
hosts. Respect that host's existing thermal limits; none are invented here.

Dependencies: Linux x86-64 GNU runtime, libvulkan.so.1 and the target machine's
actual Vulkan ICD plus glibc. Inspect linkage on the target before loading.
For source reproduction, use already installed GCC/G++, ar/make/readelf/Python3
and Zig 0.15.2. No install or credentials are authorized by this handoff.
The task owner chooses the isolated directory and binds rebuilt files to the
source-bound receipt produced by build-native.py. No public/default switch.

Run at most one bounded smoke process at a time, no unattended retry:

    env -u VK_ICD_FILENAMES -u VK_DRIVER_FILES -u LD_PRELOAD \
      DOE_BACKEND=vulkan FAWN_BACKEND_LANE=vulkan_doe_app \
      timeout --signal=TERM --kill-after=5s 30s \
      "$DIR/native-smoke-c" "$DIR/libwebgpu_doe.so" hardware

Use env-unset only for this invocation; do not change host driver configuration.
If host policy intentionally pins ICD selection, preserve its verified physical
ICD rather than applying those two -u options. The deliberately missing ICD
path used by cloud CPU tests must not be inherited. Both DOE selectors are
required: backend choice alone does not choose native drop-in ownership.

Only after a clean pass and healthy conditions, optionally repeat the same one
shot with native-smoke-cpp. Each run uses exactly two 64-byte buffers, one copy
submission, one map/readback, no dispatch loop or benchmark. Runtime setup may
allocate more driver memory. Each callback has a roughly five-second polling
budget; blocking driver calls are bounded by the outer 30-second watchdog.
If a timeout, thermal warning, failure, unknown completion or unexpected adapter
occurs, stop and retain the log. Do not retry to turn an ambiguous result green.
Confirm process/GPU quiescence through the physical-host owner's usual checks.

Expected success: backend=Vulkan with a non-CPU, non-unknown adapter, nonzero
vendor/device IDs, exit 0, and
"PASS: physical Vulkan buffer copy and exact readback; smoke only, not integrated qualification".
The public API observes the fixed 16-word input pattern byte-for-byte after copy.
A no-adapter failure is not hardware success. Exit 3 preserves uncertainty rather
than explicitly destroying still-live owners. A pass proves this one bounded
hybrid buffer-copy/readback only, not compute, graphics, timing, Dawn parity,
complete migration or acceptance of new C resource/backend work.

Cloud findings: build, C11/C++17, no-adapter callback, private ELF bridge locality
and hostile preload controls passed. Physical hardware mode is UNRUN.
