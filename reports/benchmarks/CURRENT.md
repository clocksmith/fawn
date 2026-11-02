# Current benchmark evidence

Generated from [sources.json](sources.json) by `python3 bench/cli.py inventory`.

Current product-source frontier: `8c4bc670ec5452f126adf99c4a5b315e0a99e922`.

Each family links to its complete aggregate history. Builds and dates below summarize recorded evidence; they do not bind different machines or executions together.

W/L/I counts refer to aggregate metric outcomes, not workload or campaign wins. I includes unknown, incomparable and not-classified-as-loss; none implies a win. Failed observations remain separate from metric counts. Qualification never follows from discovery.

## Native Runtime

| Family | Recorded machine / OS | Recorded builds: source/binary | Last execution / contract freeze | Metrics W/L/I; failed observations | Qualification | Current source / measurement gaps |
| --- | --- | --- | --- | --- | --- | --- |
| [macOS native 127-workload catalog](families/catalog-macos.md) (127 observations) | unbound; 127 unbound observations / darwin | doe 6258de498566/dce0ea9520c6 vs dawn unknown/9696e6d1261b | unknown / 2026-11-01T18:01:21Z | 0/442/998; 7 failed | 0 fully qualified observations | current source unmeasured; 127 undated; machine provenance missing |
| [Linux Intel native 127-workload catalog](families/catalog-intel.md) (127 observations) | unbound; 127 unbound observations / linux | doe 6258de498566/3e7cd14c2497 vs dawn unknown/479d26ddd5f9 | unknown / 2026-11-01T18:01:34Z | 0/390/1038; 8 failed | 0 fully qualified observations | current source unmeasured; 127 undated; machine provenance missing |
| [Windows D3D12 native 127-workload catalog](families/catalog-d3d12.md) (127 observations) | unbound; 127 unbound observations / win32 | doe 6258de498566/308f7fc32aae vs dawn unknown/dcc7af755828 | unknown / 2026-11-01T18:18:29Z | 0/383/673; 39 failed | 0 fully qualified observations | current source unmeasured; 127 undated; machine provenance missing |
| [Windows AMD Vulkan native 127-workload catalog](families/catalog-windows-vulkan.md) (127 observations) | unbound; 127 unbound observations / win32 | doe 6258de498566/fe2c06f04d7d vs dawn unknown/95b95efad764 | unknown / 2026-11-01T18:45:19Z | 0/194/898; 36 failed | 0 fully qualified observations | current source unmeasured; 127 undated; machine provenance missing |
| [Native Metal vector/image/upload phase cohorts](families/phase-metal.md) (420 observations) | unbound; 420 unbound observations / darwin | cand unknown/unknown vs dawn unknown/unknown; cand unknown/unknown vs main unknown/unknown; +14 pairs | 2026-11-02T02:01:06Z / 2026-11-02T01:59:38.876366Z | 92/13/7515; 0 failed | 0 fully qualified observations | current source unmeasured; 414 undated; machine provenance missing |
| [Linux AMD vector/image/upload phase experiments](families/phase-amd.md) (231 observations) | unbound; 231 unbound observations / linux | c-direct unknown/unknown vs main unknown/unknown; c-staging unknown/unknown vs main unknown/unknown; +30 pairs | 2026-10-30T16:27:50Z / 2026-10-30T02:36:48Z | 0/0/7191; 0 failed | 0 fully qualified observations | current source unmeasured; 54 undated; machine provenance missing |
| [Linux Intel vector/image/upload phase experiments](families/phase-intel.md) (94 observations) | unbound; 94 unbound observations / linux | cand unknown/unknown vs dawn unknown/unknown; cand unknown/unknown vs main unknown/unknown; +13 pairs | 2026-10-30T21:40:23Z / 2026-10-31T02:35:53Z | 0/0/3102; 0 failed | 0 fully qualified observations | current source unmeasured; 90 undated; machine provenance missing |
| [Windows Vulkan vector/image/upload phase experiments](families/phase-windows-vulkan.md) (6186 observations) | unbound; 6186 unbound observations / win32 | combined unknown/c9b3bb0be873 vs unpaired unknown/unknown; combined unknown/unknown vs dawn unknown/unknown; +29 pairs | unknown / 2026-10-31T12:36:16Z | 0/0/2970; 0 failed | 0 fully qualified observations | current source unmeasured; 6186 undated; machine provenance missing |

## Browser Integration

| Family | Recorded machine / OS | Recorded builds: source/binary | Last execution / contract freeze | Metrics W/L/I; failed observations | Qualification | Current source / measurement gaps |
| --- | --- | --- | --- | --- | --- | --- |
| [macOS Fawn/Chrome fresh-launch and retained browser diagnostics](families/browser-macos.md) (531 observations) | mars; 456 unbound observations / darwin | chrome unknown/2acf677e0b33 vs unknown unknown/unknown; chrome unknown/unknown vs unknown unknown/unknown; +7 pairs | 2026-11-02T17:54:09.850000Z / unknown | 26/119/311; 0 failed | 0 fully qualified observations | current source unmeasured; machine provenance missing |
| [Windows Fawn/Chrome fresh-launch diagnostics](families/browser-windows.md) (228 observations) | unbound; 228 unbound observations / win32 | chrome unknown/unknown vs unknown unknown/unknown; fawn_dawn unknown/unknown vs chrome unknown/unknown; +3 pairs | 2026-11-01T17:31:17.257000Z / unknown | 8/80/80; 0 failed | 0 fully qualified observations | current source unmeasured; machine provenance missing |
| [Linux Fawn/Chromium integrated comparison gaps](families/browser-linux.md) (1 observations) | unbound; 1 unbound observations / linux | unknown unknown/unknown vs unknown unknown/unknown | unknown / unknown | 0/0/0; 0 failed | 0 fully qualified observations | current source unmeasured; 1 undated; machine provenance missing; required evidence unavailable |

## Compiler

| Family | Recorded machine / OS | Recorded builds: source/binary | Last execution / contract freeze | Metrics W/L/I; failed observations | Qualification | Current source / measurement gaps |
| --- | --- | --- | --- | --- | --- | --- |
| [Compiler/Tint, UMAP and ecosystem/projection diagnostics](families/compiler-ecosystem.md) (379 observations) | unbound; 379 unbound observations / darwin, linux, win32 | dawn unknown/352a295ea99a vs unknown unknown/unknown; doe unknown/20397ca65d27 vs unknown unknown/unknown; +3 pairs | unknown / unknown | 0/0/96; 1 failed | 0 fully qualified observations | current source unmeasured; 379 undated; machine provenance missing; required evidence unavailable |

## Application

| Family | Recorded machine / OS | Recorded builds: source/binary | Last execution / contract freeze | Metrics W/L/I; failed observations | Qualification | Current source / measurement gaps |
| --- | --- | --- | --- | --- | --- | --- |
| [Historical native and Gemma64 Node/Bun/Deno comparisons](families/legacy-package-native.md) (68 observations) | 128, mac.lan; 3 unbound observations / darwin, linux, win32 | doe unknown/098c6808d890 vs dawn_delegate unknown/098c6808d890; doe unknown/09afd3d6e757 vs dawn_delegate unknown/09afd3d6e757; +6 pairs | unknown / unknown | 0/0/0; 0 failed | 0 fully qualified observations | current source unmeasured; 68 undated; machine provenance missing; required evidence unavailable |
| [ONNX/ORT native, package and browser comparisons](families/onnx-ort.md) (39 observations) | mac.lan; 37 unbound observations / darwin, linux, win32 | dawn unknown/unknown vs unknown unknown/unknown; doe unknown/unknown vs unknown unknown/unknown; +3 pairs | unknown / unknown | 0/0/14; 0 failed | 0 fully qualified observations | current source unmeasured; 39 undated; machine provenance missing; required evidence unavailable |
| [Doppler generation and memory comparisons](families/doppler.md) (12 observations) | unbound; 12 unbound observations / darwin, linux, win32 | dawn unknown/unknown vs unknown unknown/unknown; doe unknown/unknown vs unknown unknown/unknown; +1 pairs | unknown / unknown | 0/0/64; 0 failed | 0 fully qualified observations | current source unmeasured; 12 undated; machine provenance missing; required evidence unavailable |

## Workspace

| Family | Recorded machine / OS | Recorded builds: source/binary | Last execution / contract freeze | Metrics W/L/I; failed observations | Qualification | Current source / measurement gaps |
| --- | --- | --- | --- | --- | --- | --- |
| [Fawn workspace behavioral comparisons and platform compatibility](families/workspace.md) (149 observations) | unbound; 149 unbound observations / linux, darwin, win32 | control 9f522f7d9840/27e55cc30e48 vs stock-automation unknown/27e55cc30e48; treatment 9f522f7d9840/27e55cc30e48 vs stock-automation unknown/27e55cc30e48; +1 pairs | 2026-10-26T21:59:16.879000Z / unknown | 0/0/8; 15 failed | 0 fully qualified observations | current source unmeasured; 139 undated; machine provenance missing |

## Inspect and reproduce

- Machine-readable [index.json](index.json): compact v3; `bench.inventory.storage.load_inventory` expands v2/v3.
- Optional searchable dashboard: `python3 bench/cli.py inventory --dashboard-html-out bench/out/cube-inventory.html`.
- Generate: `python3 bench/cli.py inventory`; verify all generated pages: `python3 bench/cli.py inventory --check`.
- Public claim authority remains [claim-index.json](../claim-index.json).
