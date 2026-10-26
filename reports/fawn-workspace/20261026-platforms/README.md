# Fawn platform verification

Consumer: `fawnWorkspace.qualificationEvidence` and `compatibilityEvidence` in
[`doe-product-strategy.json`](../../../config/doe-product-strategy.json), enforced
by [`test_fawn_workspace_strategy.py`](../../../bench/tests/test_fawn_workspace_strategy.py).
This record also retains the native controls and the rejected Chromium boundary
needed to assess the user's all-platform verification request. It establishes
neither browser release admission nor a performance advantage.

## Workspace behavior

Every admitted folder contains all 102 planned runs of the frozen
`2026-10-26-behavioral-v3-platform-exits` case set, both Playwright and raw-CDP
adapters, and research, submission and export workflows. Journals and independent
fixture receipts remain available for semantic revalidation.

| Machine | OS / browser | Mode | Tested source | Admission |
| --- | --- | --- | --- | --- |
| Suby | Linux x64 / Chrome 136.0.7103.59 | Headless | `8001ce0afeefe1ab331ddded9c7ff85f7f4c47bb` | [102 admitted](qualification/verdict.json) |
| Suby | Linux x64 / Chrome 136.0.7103.59 | Headed, isolated Xvfb | `8001ce0afeefe1ab331ddded9c7ff85f7f4c47bb` | [102 admitted](compatibility-linux-headed/verdict.json) |
| Mars | macOS arm64 / Chrome 155.0.8059.85 | Headless | `8001ce0afeefe1ab331ddded9c7ff85f7f4c47bb` | [102 admitted](compatibility-macos/verdict.json) |
| Miles | Windows x64 / Chrome 155.0.8059.86 | Headless | `47863428f9cd4ffa986a055d658685edcb6db3e8` | [102 admitted](compatibility-windows/verdict.json) |

These are stock Chrome workspace runs. They do not exercise Doe beneath
`navigator.gpu`, qualify a Fawn Chromium distribution, or rerun the separate
product-comparison suites. Xvfb establishes a headed fixture run, not physical
display or GPU qualification. Windows ran sandboxed Chrome with the explicit
`--do-not-de-elevate` flag required by its Administrator SSH session.

The target-local `--require-current-source` checks passed before subsequent
source changes. Linux and Windows retain `current-source-verdict.json`; Mars
retains the durable [executor receipt](compatibility-macos/executor-receipt.json)
for its runner plus current-source validation command. Original `result.json`
and `verdict.json` bytes are preserved. Windows predates the profile-directory
race fix in `8001ce0`; its full run is bound to the source it actually ran.
Later offline-reader changes do not transfer those runs to newer source.

`case-set.json` retains the producer's exact case-set bytes, including Windows
CRLF. Revalidation requires its recorded SHA-256 and equality of every frozen
JSON rule with the current case set. Evidence paths are interpreted using the
producer's path grammar and confined to the retained directory. No report path,
hash, termination tuple or oracle has been rewritten for another OS.

From the repository root, revalidate any admitted folder, for example:

```sh
node browser/workspace/qualification/validate.js reports/fawn-workspace/20261026-platforms/compatibility-windows
python3 -m unittest bench.tests.test_fawn_workspace_strategy
```

Use `--require-current-source` only against the exact source checkout named by
the result. Browser images and mapped modules, platform versions, native exit
tuples, process/listener observations, source hashes and git cleanliness are in
each result. `SHA256SUMS` binds the retained files.

## Rejected runs and fixes

- [Linux's earlier full rejection](rejected-linux-lazy-recovery/verdict.json)
  retains four recovery cases where journal-only recovery never opened a browser,
  but the new strict inspector correctly required a live browser. The procedure
  now makes a real read-only observation before inspection; the strict check stays.
- [Mac's earlier full rejection](rejected-macos-profile-race/verdict.json)
  retains a transient browser-owned profile directory disappearing during
  enumeration. The fix tolerates only `ENOENT` in that temporary profile walk;
  source inventory and listener checks remain strict.
- Windows forced termination uses independently observed controller requests or
  synchronous host-hook receipts plus raw Node exit tuples. Windows controller
  and self-termination differ; ordinary exits and wrong request sources remain
  rejected by focused controls. The corrected full result is retained above.

The frozen case set retains its original cases, workflows and effect oracles;
its version changed to represent the platform-specific raw termination contract.

## Native backend controls

These controls have independent source and binary identities; they do not
inherit the workspace source or admission.

- [Mars Metal](native-macos/summary.json): physical Apple M3 public compute,
  readback, bounds/dynamic-offset and callback controls, two-device isolation,
  and offscreen rendering/upload controls passed.
- [Miles D3D12](native-windows/native-scope.json): the current C DLL and compiler
  build, export/loader and C/C++ consumer controls passed. Public compute/readback,
  two-device/WaitAny and procedural graphics controls passed on physical AMD Radeon
  Graphics and separately on WARP. WARP is a software control.
- [Miles Windows DXC](windows-dxc/summary.json): 40 process controls passed,
  including quoting/Unicode, bounded concurrent output, inherited-handle isolation,
  handle stability, failures and full unsigned exit codes. A storage-writing shader
  compiled with real DXC and passed the corrected structural DXIL validator and
  official dumpbin; invalid HLSL was rejected.
- [Suby Vulkan](native-linux/retention-summary.json): four public consumers
  (compute, instance/devices, async submit and offscreen triangle) passed on
  physical Intel UHD Graphics TGL GT2, Mesa 24.2.8, Vulkan 1.3.289.
  ASan/UBSan CPU controls passed 391 mapping/toolchain, planner and reference
  emitter checks; leak detection was disabled. These runs bind clean `d25e075`,
  not the later workspace source.

Real DXC exposed an inherited incorrect DXIL program-header layout. C and the
retained Zig producer/validator were corrected to the documented relative
bitcode offset, with independent malformed-header controls. Compiler acceptance
of a DXIL container does not prove execution of every shader.

The aggregate C `make check` was attempted and remains blocked by three existing
unregistered CSL test files. CSL is excluded from this request; no acceptance
check was weakened to obtain a pass.

## Fawn Chromium boundary: not verified on all platforms

The [Mac integration summary](chromium-macos/mars-verification-summary.json)
retains both the initial sandbox load failure and the relocated sandboxed rerun.
Bundling the current C runtime and compiler fixed sandbox loading. Dawn smoke
passed. Forced Doe still failed: the retained browser requests
`wgpuBufferCreateTexelView`, which its current source patch does not require.
All 138 procedures required by the current patch resolve. This is an unmatched
browser artifact; fake exports or hidden fallback were not added.

The configured matching Chromium source is on the unmounted
`/Volumes/MACOS/fawn-browser/src`; no source-matched alternate build was found.
Miles and Suby have no Fawn Chromium executable. A matching build is required
before forced-Doe browser, backend/browser lifecycle, or release qualification
can pass. Native results and stock Chrome workspace runs do not close that gap.

The final [Windows stock-Chrome Dawn smoke](chromium-windows/miles-stock-chrome-dawn-20261026-d.json)
passed core WebGPU readbacks, validation recovery, canvas, Fawn page lifecycle,
adapter retries and browser cleanup on AMD RDNA2, sandbox enabled. It retains
the earlier [asset-404 rejection](chromium-windows/rejected-assets-404.json).
The Windows path-containment fix made the same asset server work on the native
path grammar; that baseline still does not supply a Fawn Chromium binary.
