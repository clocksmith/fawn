#!/usr/bin/env python3
"""Generate the governed compute plan IR with command-graph CPU oracles.

The governed native rows used single repeated commands with isolated schema 1
oracles, which the plan output proof rejects. Each scenario here keeps the
row's kernel, grid and bindings, unrolls its repeats into single plan
dispatches and declares a schema 2 ``command_graph`` oracle on the final
dispatch. Expected bytes come from the CPU references below, never from a GPU
run; ``--check`` recomputes them and fails when the IR differs.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
from typing import Any, Callable

from bench.oracles import concurrent_runsingle_u32_v1 as concurrent
from bench.oracles import workgroup_recurrence_u32_v1 as recurrence


REPO_ROOT = Path(__file__).resolve().parents[2]
IR_PATH = REPO_ROOT / "bench" / "ir" / "compute_governed.json"
_DROPPED_FIELDS = {"repeat", "repeatSynchronization", "warmup_dispatch_count", "output_oracle"}


def _zero_reference(byte_length: int) -> Callable[[int], bytes]:
    # Zero inputs give zero products and sums; zero-initialized workgroup
    # memory copies out as zeros. Equal to the buffer's initial state, so these
    # oracles reject corrupted output but cannot detect a skipped dispatch.
    return lambda dispatch_count: bytes(byte_length)


# (workload id, source command stream, reference id, CPU reference)
SCENARIOS: tuple[tuple[str, str, str, Callable[[int], bytes]], ...] = (
    ("compute_workgroup_atomic_1024", "examples/workgroup_atomic_commands.json",
     "bench/oracles/workgroup_recurrence_u32_v1.py",
     lambda count: recurrence.expected_output(dispatch_count=count)),
    ("compute_workgroup_non_atomic_1024", "examples/workgroup_non_atomic_commands.json",
     "bench/oracles/workgroup_recurrence_u32_v1.py",
     lambda count: recurrence.expected_output(dispatch_count=count)),
    ("compute_concurrent_execution_single", "examples/concurrent_execution_single_commands.json",
     "bench/oracles/concurrent_runsingle_u32_v1.py",
     lambda count: concurrent.expected_output(dispatch_count=count)),
    ("compute_matvec_32768x2048_f32", "examples/matrix_vector_mul_32768x2048_commands.json",
     "sha256_zero_bytes_131072_v1", _zero_reference(131072)),
    ("compute_matvec_32768x2048_f32_swizzle1", "examples/matrix_vector_mul_32768x2048_swizzle1_commands.json",
     "sha256_zero_bytes_131072_v1", _zero_reference(131072)),
    ("compute_matvec_32768x2048_f32_workgroupshared_swizzle1",
     "examples/matrix_vector_mul_32768x2048_workgroupshared_swizzle1_commands.json",
     "sha256_zero_bytes_131072_v1", _zero_reference(131072)),
    ("pipeline_compile_stress", "examples/shader_compile_stress_commands.json",
     "sha256_zero_bytes_1048576_v1", _zero_reference(1048576)),
    ("compute_zero_initialize_workgroup_memory_256",
     "examples/zero_initialize_workgroup_memory_256_commands.json",
     "webgpu_zero_initialized_workgroup_memory_v1", _zero_reference(8192)),
)


def _source_dispatch(path: str) -> tuple[dict[str, Any], int, dict[str, Any]]:
    commands = json.loads((REPO_ROOT / path).read_text(encoding="utf-8"))
    if not isinstance(commands, list) or len(commands) != 1 or commands[0].get("kind") != "kernel_dispatch":
        raise ValueError(f"{path}: expected one kernel_dispatch command")
    source = commands[0]
    oracle = source.get("output_oracle")
    if not isinstance(oracle, dict) or oracle.get("initialization") != "zero_fill_v1":
        raise ValueError(f"{path}: expected a zero-fill output oracle")
    if source.get("initialize_buffers_on_create") is False:
        raise ValueError(f"{path}: zero-fill oracle needs zero-initialized buffers")
    repeat = source.get("repeat", 1)
    if type(repeat) is not int or repeat < 1:
        raise ValueError(f"{path}: invalid repeat")
    dispatch = {key: copy.deepcopy(value) for key, value in source.items() if key not in _DROPPED_FIELDS}
    dispatch["initialize_buffers_on_create"] = True
    return dispatch, repeat, oracle


def _target_length(dispatch: dict[str, Any], oracle: dict[str, Any]) -> int:
    targets = [b for b in dispatch.get("bindings", [])
               if b.get("group", 0) == oracle["binding_group"] and b.get("binding") == oracle["binding"]]
    if len(targets) != 1:
        raise ValueError(f"{dispatch['kernel']}: oracle binding does not resolve to one buffer")
    return int(targets[0]["buffer_size"])


def build_scenario(workload_id: str, source_path: str, reference_id: str,
                   reference: Callable[[int], bytes]) -> dict[str, Any]:
    dispatch, repeat, source_oracle = _source_dispatch(source_path)
    expected = reference(repeat)
    if len(expected) != _target_length(dispatch, source_oracle):
        raise ValueError(f"{workload_id}: reference length differs from the bound output range")
    commands: list[dict[str, Any]] = (
        [dispatch] if repeat == 1 else [{"kind": "repeat", "count": repeat, "commands": [dispatch]}])
    return {
        "id": workload_id,
        "description": (f"{dispatch['kernel']} grid {dispatch['x']}x{dispatch['y']}x{dispatch['z']}, "
                        f"{repeat} dependent plan dispatch{'es' if repeat > 1 else ''} from {source_path}; "
                        "the final dispatch carries the command-graph CPU oracle."),
        "planPath": f"bench/plans/generated/{workload_id}.plan.json",
        "commandsPath": f"bench/plans/generated/compat/{workload_id}_commands.json",
        "timingScope": "operation",
        "outputOracle": {
            "schema_version": 2,
            "scope": "command_graph",
            "reference_class": "independent_v1",
            "kind": "sha256_exact_v1",
            "initialization": "zero_fill_v1",
            "binding_group": source_oracle["binding_group"],
            "binding": source_oracle["binding"],
            "dispatch_count": 1,
            "expected_sha256": hashlib.sha256(expected).hexdigest(),
            "reference_id": reference_id,
        },
        "commands": commands,
    }


def build_ir() -> dict[str, Any]:
    return {
        "schemaVersion": 1,
        "kind": "compute_governed",
        "description": ("Governed compute rows as direct plans: repeats unrolled into single dispatches over "
                        "zero-initialized buffers, final output checked against a CPU reference in the same "
                        "execution. Generated by bench/tools/generate_governed_plan_ir.py."),
        "shared": {"kernelRoot": "bench/kernels", "timingScope": "operation"},
        "scenarios": [build_scenario(*row) for row in SCENARIOS],
    }


def render(ir: dict[str, Any]) -> str:
    return json.dumps(ir, indent=2) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Fail when the IR differs from the CPU references.")
    args = parser.parse_args()
    text = render(build_ir())
    if args.check:
        if not IR_PATH.exists() or IR_PATH.read_text(encoding="utf-8") != text:
            print(f"{IR_PATH.relative_to(REPO_ROOT)} differs from its CPU references; rerun without --check")
            return 1
        return 0
    IR_PATH.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
