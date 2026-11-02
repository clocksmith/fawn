# CATSCAN: Experimental C Vulkan backend

Parent: [Experimental C migration](../../CATSCAN.md)

## Target

Execute admitted Vulkan independently in C under resource/completion obligations.

## Authority

- Owns Vulkan loading, native allocations, descriptors, pipelines, recording, submission, retirement, selected Linux surfaces and fd imports.
- Excludes WGSL meaning, public WebGPU validation, provider selection and qualification.

## Scope

- Private backend ABI and focused tests.

## Contracts

- Inputs: compiler-validated SPIR-V, typed resource/commands: [ABI](include/doe_vk.h).
- Indirect arguments use the [shared normalizer](../../common/include/doe_draw_guard.h); native bounds/retirement stay here.
- Output: native statuses, capabilities and completion-backed readback.
- Lifecycle: [system invariants](../../../../INTENT.md).

## Invariants

- Unknown completion retains every resource; timeout is not completion.
- State publishes at accepted `vkQueueSubmit`; refusal poisons it (owner-authorized 2026-10-24).
- No interpreter, silent backend substitution or production default change.
- Adapter resource policy uses one rule
  (`doe_vk_resource_profile_select`: AMD vendor for descriptor blocks; AMD plus a
  >= 4 GiB CPU-writable device-local heap for device-local host writes; owner-authorized
  2026-10-31); other adapters keep DEFAULT unchanged.
- Pinned ABI verification, software execution and physical qualification stay distinct.
- Caller serialization/ownership transfer stay explicit.
- Acquisition/presentation fences differ from command completion; unsupported WSI never fabricates surfaces.
- Presented handles stay stale and retain their swapchain generation; replacements share one platform-surface owner and retire independently.

## Acceptance

- [Host checks](Makefile) verify ABI, rollback, invalid input and submission lifetime.
- [Native test](tests/native_compute.c) checks actual Vulkan output and early caller release.
- Physical backend/complete retained-consumer parity require separate gates.
- Evidence: [focused terminal receipt](TERMINAL_RECEIPT.txt).

## Non-goals

- WebGPU validation or promotion through compilation.

## Freedom

Internal algorithms may vary.
