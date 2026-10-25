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
Linux/Darwin staging performs no Zig build, external shader compilation, installation,
package download, GPU work or system configuration change. Vulkan is loaded only by actual
backend consumers; this packaging check creates an instance but no device.

Windows uses the existing x64 Windows SDK developer environment and `clang-cl`:

```powershell
python runtime/c/tools/build-native-package.py --ref HEAD --build C:\doe-build\fresh --destination C:\doe-stage\fresh --dxc-root C:\doe-tools\dxc
```

Supply the official DXC SDK already used by the native Windows builder. That
builder owns source selection, DLL/export checks and compiler host controls.
Staging includes the native and compiler DLLs, both DXC runtime DLLs, import
libraries and all upstream notices. DLLs reside beside the consumer in `bin/`.
The PE dependency audit rejects missing non-system DLLs. Relocated consumption
runs from an empty directory with `PATH` restricted to Windows System32; build
and SDK paths cannot hide missing packaged dependencies. Existing system runtime
DLLs remain prerequisites. No adapter or device is requested. The focused
`c-windows-package` workflow retains actual SDK build and relocation results;
[Windows developer evidence](DEVELOPER_RECEIPT_PACKAGE_WINDOWS.json) records a
source-bound pass at `b9d016fc0`: Windows Server 2025, Clang 20.1.8, 35
independently rehashed package members and 199 implemented WebGPU exports.
Both initial archive/bytecode failures are retained; no device was used.

The tool captures the named committed tree, never concurrent worktree edits. It
calls the native owner's platform builder and its compiler source/flag owner rather than
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
staging, not a system installer, npm/Node addon, Fawn/browser integration,
GPU execution or full release qualification. Do not replace
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

On 2026-10-23 on Jupiter (Radeon 8060S, RADV, Node 22.22.1), with the package
staged at 2cd557c9f, `npm run test:integration` (node runtime, 33 entrypoints)
passed 29. The four failures need compiler coverage (`var<private>`, builtin calls
in const initializers, zero-iteration loops, `unpack4xU8`, identifier admission);
native-reflection also tests the Zig-only Doe binding-reflection extension. The
installed-package tests skip under a `DOE_WEBGPU_LIB` override, since they check
the platform package's own library. This is not npm packaging, a published
prebuild, or release evidence.

[Apple public compute evidence](DEVELOPER_RECEIPT_NODE_METAL.json) records the
unchanged Node first-kernel example through staged C source `00bbd786d` and real
Metal dispatch/readback: 16 checks, zero failures or skips. The selected C artifact
does not borrow Zig prebuild proof metadata. This one compute case does not
qualify later native source, graphics, presentation, performance or production use.
