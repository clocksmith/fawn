# CATSCAN: Experimental C Vulkan backend

Parent: [Experimental C migration](../../CATSCAN.md)

## Target

Execute admitted native Vulkan work through an independently usable C owner while preserving resource and completion obligations.

## Authority

- Owns experimental Vulkan loader, native allocations, descriptors, pipelines, command recording, submission, retirement and selected native Linux surface ownership.
- Does not own WGSL meaning, public WebGPU validation, provider selection or qualification.

## Scope

- Includes this directory's private backend ABI and focused tests.

## Contracts

- Input: compiler-validated SPIR-V and explicit typed resource/command descriptions in [the boundary](include/doe_vk.h).
- Indirect draw arguments use the [shared normalizer source](../../common/include/doe_draw_guard.h); native bounds and retirement stay here.
- Output: native statuses, capability observations and completion-backed readback.
- Lifecycle: [system invariants](../../../../INTENT.md).

## Invariants

- Unknown completion retains every resource; timeout is not completion.
- State publishes at accepted `vkQueueSubmit`; refusal poisons it (owner-authorized 2026-10-24).
- No interpreter, silent backend substitution or production default change.
- Pinned ABI verification, software execution and physical qualification remain distinct.
- Caller serialization and ownership transfer remain explicit.
- Acquisition and presentation fences are distinct from command completion; unsupported WSI never fabricates a surface.
- Presented frame handles remain stale while retaining their swapchain generation; replacement generations share one platform-surface owner and retire independently.

## Acceptance

- [Host checks](Makefile) verify ABI, rollback, invalid input and submission lifetime.
- [Native test](tests/native_compute.c) checks actual Vulkan output and early caller release.
- Physical backend and complete retained consumer parity remain separate gates.
- Evidence: [focused terminal receipt](TERMINAL_RECEIPT.txt); physical execution remains unqualified.

## Non-goals

- Replacing native WebGPU validation or promoting authored code through compilation alone.

## Freedom

Any implementation is permitted if it preserves these boundaries and passes the acceptance evidence.
