# Initial qualification target: Strix Halo workstation

**Label: initial qualification target.** This workstation is Doe's initial
engineering target. It is not a qualified or supported product configuration,
and keeps this label until the applicable acceptance passes for a named surface.

[`host-record.json`](host-record.json) freezes the machine. It was produced by:

```bash
python3 -m bench.runners.probe_vulkan_host_profile --profile linux_strix_halo_radv \
  --host-record --bind <artifact> ...
```

The profile is `linux_strix_halo_radv` in
[`config/vulkan-host-profiles.json`](../../../../config/vulkan-host-profiles.json).
The record contains:
- **CPU and platform:** AMD Ryzen AI Max+ 395 with Radeon 8060S, firmware
  (platform BIOS and GPU VBIOS), operating system and kernel.
- **Drivers:** driver package versions, and the selected Vulkan ICD manifest and
  RADV library, identified by hash.
- **Resource limits:** process limits.

Three memory quantities stay separate:

- **System RAM**: online physical memory and the kernel's `MemTotal`.
- **GPU allocation limits**: the firmware carve-out, the amdgpu GTT and TTM
  limits, and the Vulkan heap sizes and limits RADV reports. These are ceilings,
  not usage.
- **Measured residency**: `null` here. Residency belongs to per-run receipts,
  and this record measures none.

## Bound evidence

`hostRecord.bindings` binds existing artifacts by SHA-256:

| Artifact | What it supports on this target |
| --- | --- |
| [CTS subset](../20260810T222323Z/webgpu-cts-subset.json) | Only the recorded CTS queries for the bound adapter and driver; its run predates the frozen kernel |
| [Installed generation](../20261005-installed-generation/README.md) (`installation.json`, `providers.json`, `contract.json`) | The installed Doppler workload, Doe and Dawn binary identities, and the selected behaviors it records; it is not a performance comparison |
| [ONNX device lifecycle](../20261008-onnx-vulkan-device-lifecycle/README.md) (`manifest.json`) | Its recorded lifecycle qualification scope |

The CTS adapter identity matches the frozen device (`deviceID` 0x1586) and
driver (`driverVersion` 26.0.3). Its run is recorded on kernel
`7.0.0-28-generic`, while this record freezes `7.0.0-29-generic`. None of
these artifacts grants blanket support. A changed kernel, Mesa, firmware or GPU
limit is a new tuple and must be recorded again.
