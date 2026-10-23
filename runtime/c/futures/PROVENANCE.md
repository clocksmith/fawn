# Futures source provenance

SPDX-License-Identifier: MIT. Copyright (c) 2026 Doe contributors.
The repository's [MIT license](../../../LICENSE) applies to this component.

Behavior is derived from the original Doe CPU owner, not external runtime code:

- Baseline: `665475907494d2095a760596218b4c2d363cb436`.
- Owner: `runtime/zig/src/native/support/doe_future_ids.zig`.
- SHA-256: `be44acc738915f39412cfc4108f8204e31acbe59551fbc92c613d67d9ad59e92`.
- Consumer trace: `doe_callback_delivery.zig` owns allocation/payload and instance
  leases; `doe_instance_device_native.zig` retains instance across waits/pumps.
- The source owner's six inline tests remain unchanged in separate retained-Zig
  characterization. They are not linked or invoked by C builds or native consumers.
- The [contract](CONTRACT.md) names boundary adaptations and excluded owners.

Reproduce source characterization from the repository root, using an existing
approved Zig 0.15.2 executable and a NEW directory for the output:

```
python3 runtime/c/futures/tests/characterize_zig.py \
  --zig /path/to/zig-0.15.2/zig --evidence /path/to/new-evidence
```

The script copies and hashes the actual literal Zig import closure and pinned
webgpu.h without rewriting source. One import-root test plus six owner tests are
expected. Its explicit command, source hashes and original run log bind the
characterization. The C build never invokes this script.
