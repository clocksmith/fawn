"""Physical Windows CLI storage regression; explicit output directory, no installs.

Run after building doe-runtime. DXC must already be on PATH. Generated inputs and
receipts stay in --out. This qualifies bounded correctness, not paired performance.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    runtime = args.runtime.resolve()
    receipt = {"runtimeSha256": digest(runtime), "scope": "bounded D3D12 correctness only", "runs": []}

    def run(label, commands, statuses, oracles=0):
        if isinstance(commands, Path):
            command_file = commands
        else:
            command_file = output / (label + "-commands.json")
            command_file.write_text(json.dumps(commands, indent=2), encoding="utf-8")
        meta = output / (label + "-meta.json")
        rows = output / (label + "-trace.jsonl")
        command = [str(runtime), "--commands", str(command_file), "--quirks",
                   str(repo / "examples/quirks/windows_d3d12_noop_list.json"),
                   "--vendor", "generic", "--api", "d3d12", "--family", "d3d12",
                   "--driver", "1.0.0", "--backend", "native", "--backend-lane",
                   "d3d12_doe_comparable", "--execute", "--kernel-root",
                   str(repo / "bench/kernels"), "--trace-meta", str(meta),
                   "--trace-jsonl", str(rows)]
        if oracles:
            command.append("--validate-output-oracles")
        result = subprocess.run(command, cwd=repo, capture_output=True, text=True, timeout=30)
        (output / (label + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        trace = json.loads(meta.read_text()) if meta.exists() else {}
        events = [json.loads(line) for line in rows.read_text().splitlines()] if rows.exists() else []
        actual = [event.get("executionStatus") for event in events]
        accepted = (result.returncode == 0 and actual == statuses
                    and trace.get("backendId") == "doe_d3d12"
                    and trace.get("fallbackUsed") is False
                    and trace.get("outputOracleMatchedCount") == oracles
                    and trace.get("outputOracleFailedCount") == 0
                    and all(event.get("executionSubmitCount") == (1 if status == "ok" else 0)
                            for event, status in zip(events, statuses)))
        receipt["runs"].append({"label": label, "command": command,
                                "commandsSha256": digest(command_file), "exitCode": result.returncode,
                                "accepted": accepted, "statuses": actual, "trace": trace})
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
        print(label, "PASS" if accepted else "FAIL", flush=True)
        if not accepted:
            raise RuntimeError(f"{label}: inspect retained trace and log in {output}")

    def shader(name, body, element="u32", length=""):
        path = output / (name + ".wgsl")
        path.write_text(f"@group(0) @binding(0) var<storage, read_write> values: array<{element}{length}>;\n"
                        f"@compute @workgroup_size(8) fn main(@builtin(global_invocation_id) gid: vec3<u32>) {{ {body} }}\n",
                        encoding="utf-8")
        receipt.setdefault("shaders", {})[str(path)] = digest(path)
        return path

    def dispatch(source, handle=7, repeat=1):
        return {"kind": "kernel_dispatch", "kernel": str(source), "x": 1, "y": 1, "z": 1,
                "repeat": repeat, "warmup_dispatch_count": 0, "initialize_buffers_on_create": True,
                "bindings": [{"binding": 0, "group": 0, "kind": "buffer", "buffer_type": "storage",
                              "resource_handle": handle, "buffer_size": 32, "visibility": "compute"}]}

    def oracle(command, multiplier):
        result = copy.deepcopy(command)
        expected = struct.pack("<8I", *(multiplier * (i + 1) for i in range(8)))
        result["output_oracle"] = {"schema_version": 2, "reference_class": "independent_v1", "kind": "sha256_exact_v1", "scope": "command_graph",
                                   "initialization": "zero_fill_v1", "binding_group": 0, "binding": 0,
                                   "dispatch_count": command["repeat"], "expected_sha256": hashlib.sha256(expected).hexdigest(),
                                   "reference_id": f"independent-eight-lane-linear-{multiplier}-v1"}
        return result

    run("unchanged-atomic", repo / "examples/workgroup_atomic_commands.json", ["ok"], 1)
    inc = shader("increment", "values[gid.x] = values[gid.x] + gid.x + 1u;")
    twice = shader("double", "values[gid.x] = values[gid.x] * 2u;")
    # All commands request initialization. Existing handles must retain data.
    graph = [dispatch(inc, repeat=2), dispatch(inc, handle=8), oracle(dispatch(inc, repeat=3), 5),
             oracle(dispatch(twice, handle=8), 2), oracle(dispatch(inc), 6)]
    run("handle-rebind-pipeline-reuse", graph, ["ok"] * 5, 3)
    bad_stride = shader("aggregate", "values[gid.x] = vec4<u32>(1u);", "vec4<u32>", ", 8")
    run("rejected-layout-recovery", [dispatch(inc), dispatch(bad_stride), dispatch(inc)], ["ok", "unsupported", "ok"])
    oversized = dispatch(inc)
    oversized["x"] = 2
    run("rejected-dispatch-recovery", [dispatch(inc), oversized, dispatch(inc)], ["ok", "unsupported", "ok"])
    fixed = shader("fixed-too-large", "values[gid.x] = 1u;", length=", 16")
    run("rejected-fixed-extent-recovery", [dispatch(inc), dispatch(fixed), dispatch(inc)], ["ok", "error", "ok"])
    print("Physical CLI storage acceptance complete; no paired speed claim.", flush=True)


if __name__ == "__main__":
    main()
