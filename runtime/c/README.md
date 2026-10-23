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

Both paths must be fresh and disjoint. Requires Python3.12+, Git, Make and
C11/C++17 compilers. Linux uses GCC/G++14-compatible compilation, binutils and
ordinary system libc/libm/pthreads/dl. Darwin uses Apple Clang, the macOS SDK
and cctools (`otool`/`nm`); pass `CC=clang CXX=clang++` to the command above.
No Zig, external shader compiler, installation, package download,
GPU or system configuration change is performed. Vulkan is loaded only by actual
backend consumers; this packaging check creates an instance but no device.

The tool captures the named committed tree, never concurrent worktree edits. It
calls the native owner's Makefile and its compiler source/flag owner rather than
maintaining another source list. The two shared libraries have a relative loader
path: `$ORIGIN` on Linux, `@loader_path` with `@rpath` library identities on
Darwin. Darwin staging also verifies the entire non-system dylib dependency
closure and rejects absolute loader paths. A pinned-header C++ procedure consumer
runs after relocation with LD_LIBRARY_PATH, LD_PRELOAD and all DYLD_ overrides
removed. The existing export audit checks exact
symbols and procedure addresses. Compiler/header/provider identity, tool versions,
commands and all staged artifact hashes are in MANIFEST.json. The executing
packaging tool's hash is recorded separately from the captured tree; testing an
uncommitted tool never relabels the archived source as containing that tool.

The stage contains lib/, include/, licenses/, an executable host consumer, its
source, and the explicit implemented-symbol list. Missing symbols are not stubs.
MANIFEST.json is copied last; its absence means staging did not finish. Failures
preserve build logs and partial new output for diagnosis and never overwrite an
existing destination. Copies are hash-checked before completion. This is developer
staging, not a system installer, npm/Node addon, Fawn/browser integration, Windows
distribution, GPU execution or full release qualification. Do not replace
an existing application provider automatically with this artifact.

The later `DEVELOPER_RECEIPT_PACKAGE_METAL.json` repeats the actual canonical
Linux source build and relocated typed C++ consumption at source2514afd7, after
composing the explicit Metal source/metadata path and guarded Vulkan indirect
backend. This tests merged linking/packaging, not GPU execution. Historical Linux
receipts do not qualify changed source or Darwin artifacts. Apple SDK compilation,
Mach-O relocation and device execution remain separate evidence scopes.

[Darwin developer evidence](DEVELOPER_RECEIPT_PACKAGE_DARWIN.json) records the
Apple Clang/macOS SDK 26.0 build of committed native source `00bbd786d`, verified
dylib identities/dependencies, moved and staged C++ consumption, and 182 exact
exports with 20 pinned unprovided names. It identifies the locally modified
packaging tool separately. No GPU/device execution was performed.

[Current Linux regression](DEVELOPER_RECEIPT_PACKAGE_LINUX_CURRENT.json) builds
the same committed native source with the current packaging tool, relocates the
ELF package, and checks the C++ consumer and 182/20 export inventory.

## Unchanged JavaScript package consumer

`make -C runtime/c check-js STAGE=<finished stage>` rebuilds the doe-gpu N-API addon
(gitignored `packages/doe-gpu/build`) and runs the unchanged first-kernel
integration test with `DOE_WEBGPU_LIB` pointing at the staged
`libwebgpu_doe_c.so`. The package requests a nonfallback adapter, so this needs a
physical GPU; it is developer evidence on the adapter the receipt names.

The addon prefers Doe-specific `doeNative*` fast paths when a provider exports them
(the Zig provider) and otherwise calls the exact standard `wgpu*` procedure; a
missing implementation throws rather than doing nothing. Binding reflection is
optional: without it the pipeline's own auto layout is used. The package reports
this provider as `libraryFlavor: "doe-c"`.

On 2026-10-24 on Jupiter (Radeon 8060S, RADV, Node 22.22.1) the staged package at
91972c24d passed first-kernel 16/16 and 20 of the package's 32 node integration
tests. The remaining failures need reusable compute programs and GPU recording in
C, the Doe binding-reflection extension, compiler coverage, depth copies, or are
packaging-harness artifacts (a packed tarball's own prebuilt addon, and a permission
sandbox that denies an out-of-package library path). This is not npm packaging, a
published prebuild, or release evidence.
