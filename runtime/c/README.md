# C developer build and staging

This is the explicit, partial C provider, not a production-default switch or a
complete replacement package. Read [the common guide](STYLE.md),
[migration status](../../docs/c-migration/MIGRATION.md), and the
[native owner's support boundary](native/README.md).

From the repository root:

```sh
make -C runtime/c check-tools
make -C runtime/c package REVISION=HEAD \
  BUILD=/tmp/doe-c-build-unique STAGE=/tmp/doe-c-stage-unique
```

Both paths must be fresh and disjoint. Requires Linux, Python3.12+, Git, GNU Make,
GCC/G++14-compatible C11/C++17 compilation, binutils and ordinary system libc/libm/
pthreads/dl. No Zig, external shader compiler, installation, package download,
GPU or system configuration change is performed. Vulkan is loaded only by actual
backend consumers; this packaging check creates an instance but no device.

The tool captures the named committed tree, never concurrent worktree edits. It
calls the native owner's Makefile and its compiler source/flag owner rather than
maintaining another source list. The two shared libraries have a relative loader
path. A pinned-header C++ procedure consumer runs after relocation with
LD_LIBRARY_PATH and LD_PRELOAD removed. The existing export audit checks exact
symbols and procedure addresses. Compiler/header/provider identity, tool versions,
commands and all staged artifact hashes are in MANIFEST.json.

The stage contains lib/, include/, licenses/, an executable host consumer, its
source, and the explicit implemented-symbol list. Missing symbols are not stubs.
MANIFEST.json is copied last; its absence means staging did not finish. Failures
preserve build logs and partial new output for diagnosis and never overwrite an
existing destination. Copies are hash-checked before completion. This is developer
staging, not a system installer, npm/Node addon, Fawn/browser integration, Windows
or macOS distribution, GPU execution or full release qualification. Do not replace
an existing application provider automatically with this artifact.
