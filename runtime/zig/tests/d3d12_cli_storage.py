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

    def run(label, commands, statuses, oracles=0, submissions=None):
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
        expected_submissions = submissions or [1 if status == "ok" else 0 for status in statuses]
        accepted = (result.returncode == 0 and actual == statuses
                    and trace.get("backendId") == "doe_d3d12"
                    and trace.get("fallbackUsed") is False
                    and trace.get("outputOracleMatchedCount") == oracles
                    and trace.get("outputOracleFailedCount") == 0
                    and all(event.get("executionSubmitCount") == count
                            for event, count in zip(events, expected_submissions)))
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

    def oracle(command, multiplier=1, values=None):
        result = copy.deepcopy(command)
        words = values if values is not None else [multiplier * (i + 1) for i in range(8)]
        expected = struct.pack(f"<{len(words)}I", *words)
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
    def write(words, offset=0, handle=7, size=32):
        return {"kind": "buffer_write", "handle": handle, "offset": offset, "buffer_size": size, "data": words}

    initial = [5, 11, 13, 19, 23, 29, 31, 37]
    first = [value + 3 * (i + 1) for i, value in enumerate(initial)]
    second = first.copy()
    second[2:4] = [99, 101]
    second = [value + i + 1 for i, value in enumerate(second)]
    seeded = [write(initial), oracle(dispatch(inc, repeat=3), values=first),
              write([99, 101], offset=8), oracle(dispatch(inc), values=second)]
    run("nonzero-seed-partial-update", seeded, ["ok"] * 4, 2)
    identity = shader("identity", "values[gid.x] = values[gid.x] + 0u;")
    large = [(i * 17 + 3) & 0xffffffff for i in range(16386)]
    check = dispatch(identity)
    check["bindings"][0]["buffer_size"] = len(large) * 4
    run("chunked-nonzero-write", [write(large, size=len(large) * 4), oracle(check, values=large)],
        ["ok", "ok"], 1, submissions=[2, 1])
    run("write-created-buffer", [write(initial, handle=99)], ["ok"], submissions=[2])
    run("write-range-recovery", [write(initial), write([99, 100], offset=28), dispatch(inc)], ["ok", "error", "ok"])
    # The canonical matrix command and WGSL are unchanged, including 50 repeats.
    matrix_file = repo / "examples/shader_compile_stress_commands.json"
    matrix = json.loads(matrix_file.read_text())[0]
    receipt.setdefault("shaders", {})[str(repo / "bench/kernels" / matrix["kernel"])] = digest(repo / "bench/kernels" / matrix["kernel"])
    run("unchanged-matrix", matrix_file, ["ok"], 1)
    identity_words = [0x3f800000 if row == col else 0 for row in range(512) for col in range(512)]
    # Distinct rows and columns expose transposed inputs or stale binding tables.
    pattern = [float((row * 3 + col * 7) % 31 + 1) for row in range(512) for col in range(512)]
    pattern_bytes = struct.pack(f"<{len(pattern)}f", *pattern)
    pattern_words = list(struct.unpack(f"<{len(pattern)}I", pattern_bytes))
    expected_hash = hashlib.sha256(pattern_bytes).hexdigest()
    seeded_matrix = copy.deepcopy(matrix)
    seeded_matrix["output_oracle"].update(schema_version=2, reference_class="independent_v1",
                                         scope="command_graph", expected_sha256=expected_hash,
                                         reference_id="independent-identity-times-row-column-pattern-f32-512-v1")
    seed = [write(identity_words, handle=7201, size=1048576), write(pattern_words, handle=7202, size=1048576)]
    # Also switch root layouts away and back while preserving seeded resources.
    run("seeded-matrix-layout-reuse", seed + [seeded_matrix, oracle(dispatch(inc)), seeded_matrix],
        ["ok"] * 5, 3, submissions=[16, 16, 1, 1, 1])
    swapped = copy.deepcopy(seeded_matrix)
    swapped["bindings"][0]["resource_handle"] = 7202
    swapped["bindings"][1]["resource_handle"] = 7201
    swapped["output_oracle"]["reference_id"] = "independent-row-column-pattern-times-identity-f32-512-v1"
    # Reordered JSON entries must still resolve to shader binding numbers.
    swapped["bindings"].reverse()
    run("matrix-handle-rebind", seed + [seeded_matrix, swapped, seeded_matrix],
        ["ok"] * 5, 3, submissions=[16, 16, 1, 1, 1])
    dense_a = [float(row % 7 + 1) for row in range(512) for col in range(512)]
    dense_b = [float(col % 5 + 1) for row in range(512) for col in range(512)]
    dense_result = [float(512 * (row % 7 + 1) * (col % 5 + 1)) for row in range(512) for col in range(512)]
    def float_words(values):
        return list(struct.unpack(f"<{len(values)}I", struct.pack(f"<{len(values)}f", *values)))
    dense = copy.deepcopy(seeded_matrix)
    dense["output_oracle"]["expected_sha256"] = hashlib.sha256(struct.pack(f"<{len(dense_result)}f", *dense_result)).hexdigest()
    dense["output_oracle"]["reference_id"] = "independent-512-term-integer-exact-dense-product-v1"
    run("dense-matrix-accumulation", [write(float_words(dense_a), handle=7201, size=1048576),
        write(float_words(dense_b), handle=7202, size=1048576), dense],
        ["ok"] * 3, 1, submissions=[16, 16, 1])
    alias = copy.deepcopy(matrix)
    alias["bindings"][2]["resource_handle"] = 7201
    access = copy.deepcopy(matrix)
    access["bindings"][0]["buffer_type"] = "storage"
    run("matrix-rejected-alias-access-recovery", [matrix, alias, access, matrix],
        ["ok", "unsupported", "unsupported", "ok"])
    flow_cases = [
        ("for-continue", "var total = 0u; for (var i = 0u; i < 4u; i++) { if (i == 1u) { i++; continue; } total += i; } values[gid.x] = total;", 3),
        ("nested-while-continue", "var total = 0u; for (var i = 0u; i < 4u; i++) { var j = 0u; while (j < 2u) { j++; if (j == 1u) { continue; } total += 10u; } if (i == 1u) { i++; continue; } total += i; } values[gid.x] = total;", 33),
        ("switch-for-continue", "var total = 0u; for (var i = 0u; i < 4u; i++) { switch i { case 1u: { i++; continue; } default: {} } total += i; } values[gid.x] = total;", 3),
        ("loop-continuing-break", "var total = 0u; var i = 0u; loop { if (i == 1u) { i++; continue; } total += i; continuing { i++; break if i >= 4u; } } values[gid.x] = total;", 3),
    ]
    flow_cases.append(("switch-continuing-break", "var total = 0u; var i = 0u; loop { switch i { case 1u: { i += 2u; continue; } default: {} } total += i; continuing { i++; break if i >= 4u; } } values[gid.x] = total;", 0))
    for label, body, expected in flow_cases:
        source = shader(label, body)
        run(label, [oracle(dispatch(source), values=[expected] * 8)], ["ok"], 1)
    block_cases = [
        ("lexical-block-shadow", "var value = gid.x + 1u; var total = 0u; { var value = gid.x + 10u; total = value; } total += value; values[gid.x] = total;", [2*i+11 for i in range(8)]),
        ("sibling-block-shadow", "var value = gid.x + 1u; var total = 0u; { var value = gid.x + 10u; total += value; } { var value = gid.x + 20u; total += value; } total += value; values[gid.x] = total;", [3*i+31 for i in range(8)]),
        ("switch-case-locals", "var total = 0u; switch (gid.x % 2u) { case 0u: { let value = gid.x + 10u; total = value; } default: { let value = gid.x + 20u; total = value; } } values[gid.x] = total;", [i+(10 if i%2==0 else 20) for i in range(8)]),
        ("continuing-body-local", "var total = 0u; var i = 0u; loop { let step = 1u; total += i; continuing { i += step; break if i >= 4u; } } values[gid.x] = total;", [6]*8),
    ]
    for label, body, expected in block_cases:
        run(label, [oracle(dispatch(shader(label, body)), values=expected)], ["ok"], 1)
    shadow_cases = [
        ("continue-shadowed-loop-index", "var trips = 0u; for (var i = 0u; i < 4u; i++) { trips++; if (trips >= 6u) { break; } { var i = 100u; continue; } } values[gid.x] = trips;"),
        ("continue-shadowed-step", "var i = 0u; var trips = 0u; let step = 1u; loop { trips++; if (trips >= 6u) { break; } { let step = 10u; continue; } continuing { i += step; break if i >= 4u; } } values[gid.x] = trips;"),
    ]
    for label, body in shadow_cases:
        run(label, [oracle(dispatch(shader(label, body)), values=[4]*8)], ["ok"], 1)
    print("Physical CLI storage acceptance complete; no paired speed claim.", flush=True)


if __name__ == "__main__":
    main()
