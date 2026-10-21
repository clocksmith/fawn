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
    # Fixed arithmetic/bit-pattern oracles check old and updated values independently.
    # Captured pointers must keep tid even after the source index is changed to zero.
    rmw_cases = [
        ("u32", "Add", "4294967280u + tid", "32u", [4294967280 + i for i in range(8)], list(range(16, 24))),
        ("u32", "Sub", "tid", "3u", list(range(8)), [4294967293, 4294967294, 4294967295, 0, 1, 2, 3, 4]),
        ("u32", "Min", "2147483648u + tid", "2147483647u", [2147483648 + i for i in range(8)], [2147483647] * 8),
        ("u32", "Max", "2147483392u + tid", "2147483648u", [2147483392 + i for i in range(8)], [2147483648] * 8),
        ("u32", "And", "240u + tid", "15u", list(range(240, 248)), list(range(8))),
        ("u32", "Or", "tid", "240u", list(range(8)), list(range(240, 248))),
        ("u32", "Xor", "240u + tid", "15u", list(range(240, 248)), list(range(255, 247, -1))),
        ("u32", "Exchange", "10u + tid", "2147483648u", list(range(10, 18)), [2147483648] * 8),
        ("i32", "Add", "-8 + i32(tid)", "3", list(range(-8, 0)), list(range(-5, 3))),
        ("i32", "Sub", "-8 + i32(tid)", "3", list(range(-8, 0)), list(range(-11, -3))),
        ("i32", "Min", "-8 + i32(tid)", "-4", list(range(-8, 0)), [-8, -7, -6, -5, -4, -4, -4, -4]),
        ("i32", "Max", "-8 + i32(tid)", "-4", list(range(-8, 0)), [-4, -4, -4, -4, -4, -3, -2, -1]),
        ("i32", "And", "-16 + i32(tid)", "15", list(range(-16, -8)), list(range(8))),
        ("i32", "Or", "-16 + i32(tid)", "8", list(range(-16, -8)), list(range(-8, 0))),
        ("i32", "Xor", "-16 + i32(tid)", "15", list(range(-16, -8)), list(range(-1, -9, -1))),
        ("i32", "Exchange", "-8 + i32(tid)", "-123", list(range(-8, 0)), [-123] * 8),
    ]
    for element, operation, initial, operand, old, updated in rmw_cases:
        label = "indexed-atomic-" + operation.lower() + "-" + element
        path = output / (label + ".wgsl")
        path.write_text(f"var<workgroup> shared_values: array<atomic<{element}>, 8>;\n"
                        "@group(0) @binding(0) var<storage, read_write> values: array<u32, 16>;\n"
                        "@compute @workgroup_size(8) fn main(@builtin(local_invocation_index) tid: u32) {\n"
                        f"  atomicStore(&shared_values[tid], {initial}); workgroupBarrier();\n"
                        "  var index = tid; let pointer = &shared_values[index]; index = 0u;\n"
                        f"  let previous = atomic{operation}(pointer, {operand}); workgroupBarrier();\n"
                        "  values[tid * 2u] = bitcast<u32>(previous);\n"
                        "  values[tid * 2u + 1u] = bitcast<u32>(atomicLoad(&shared_values[tid]));\n}\n",
                        encoding="utf-8")
        receipt.setdefault("shaders", {})[str(path)] = digest(path)
        command = dispatch(path)
        command["bindings"][0]["buffer_size"] = 64
        words = [value & 0xffffffff for pair in zip(old, updated) for value in pair]
        command = oracle(command, values=words)
        command["output_oracle"]["reference_id"] = "independent-old-new-" + label + "-v1"
        run(label, [command], ["ok"], 1)
    # Shared contention: returned old values must form a permutation of 0..7.
    # Accumulating their bit masks and sum detects lost updates and wrong returns.
    contention = output / "indexed-atomic-contention.wgsl"
    contention.write_text("var<workgroup> shared_values: array<atomic<u32>, 3>;\n"
                          "@group(0) @binding(0) var<storage, read_write> values: array<u32, 8>;\n"
                          "@compute @workgroup_size(8) fn main(@builtin(local_invocation_index) tid: u32) {\n"
                          "  if (tid == 0u) { atomicStore(&shared_values[0], 0u); atomicStore(&shared_values[1], 0u); atomicStore(&shared_values[2], 0u); }\n"
                          "  workgroupBarrier(); let old = atomicAdd(&shared_values[0], 1u);\n"
                          "  let mask_before = atomicOr(&shared_values[1], 1u << old);\n"
                          "  let sum_before = atomicAdd(&shared_values[2], old); workgroupBarrier();\n"
                          "  values[tid] = atomicLoad(&shared_values[0]) + 256u * atomicLoad(&shared_values[1]) + 65536u * atomicLoad(&shared_values[2]);\n}\n",
                          encoding="utf-8")
    receipt.setdefault("shaders", {})[str(contention)] = digest(contention)
    check = oracle(dispatch(contention), values=[1900296] * 8)  # 8 + 256*255 + 65536*28
    check["output_oracle"]["reference_id"] = "independent-eight-ticket-count-mask-sum-v1"
    run("indexed-atomic-contention", [check], ["ok"], 1)
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
        ("source-loop-flag-name", "var doe_loop_break_5 = 0u; for (var i = 0u; i < 3u; i++) { doe_loop_break_5 += 1u; } values[gid.x] = doe_loop_break_5;", 3),
        ("for-continue", "var total = 0u; for (var i = 0u; i < 4u; i++) { if (i == 1u) { i++; continue; } total += i; } values[gid.x] = total;", 3),
        ("nested-while-continue", "var total = 0u; for (var i = 0u; i < 4u; i++) { var j = 0u; while (j < 2u) { j++; if (j == 1u) { continue; } total += 10u; } if (i == 1u) { i++; continue; } total += i; } values[gid.x] = total;", 33),
        ("switch-for-continue", "var total = 0u; for (var i = 0u; i < 4u; i++) { switch i { case 1u: { i++; continue; } default: {} } total += i; } values[gid.x] = total;", 3),
        ("loop-continuing-break", "var total = 0u; var i = 0u; loop { if (i == 1u) { i++; continue; } total += i; continuing { i++; break if i >= 4u; } } values[gid.x] = total;", 3),
    ]
    flow_cases.extend([
        ('continuing-switch', 'var i = 0u; var total = 0u; loop { i++; if (i == 2u) { continue; } total += i; continuing { switch i { case 1u: { total += 10u; } default: {} } break if i >= 3u; } } values[gid.x] = total;', 14),
        ('continuing-nested-loop', 'var i = 0u; var total = 0u; loop { i++; if (i < 3u) { continue; } total += i; continuing { var j = 0u; loop { j++; if (j >= 2u) { break; } } total += j; break if i >= 3u; } } values[gid.x] = total;', 9),
        ('continuing-local', 'var i = 0u; var total = 0u; loop { if (i == 1u) { i++; continue; } total += i; continuing { let step = 1u; i += step; break if i >= 4u; } } values[gid.x] = total;', 3),
        ('break-skips-continuing', 'var i = 0u; var total = 0u; loop { total++; if (total == 2u) { break; } continuing { i += 10u; } } values[gid.x] = i;', 10),
        ('nested-for-continue', 'var total = 0u; for (var i = 0u; i < 3u; i++) { for (var j = 0u; j < 4u; j++) { if (j == 1u) { j++; continue; } total += j; } } values[gid.x] = total;', 9),
    ])
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
    sparse = shader("sparse-slot-three", "values[gid.x] += gid.x + 1u;")
    sparse.write_text(sparse.read_text().replace("@binding(0)", "@binding(3)"), encoding="utf-8")
    receipt["shaders"][str(sparse)] = digest(sparse)
    sparse_command = dispatch(sparse)
    sparse_command["bindings"][0]["binding"] = 3
    def sparse_oracle(command, slot, values):
        result = oracle(command, values=values)
        result["output_oracle"]["binding"] = slot
        return result
    run("sparse-dense-layout-reuse", [sparse_oracle(sparse_command, 3, list(range(1, 9))),
        oracle(dispatch(inc), 2), sparse_oracle(sparse_command, 3, [3*(i+1) for i in range(8)])], ["ok"]*3, 3)
    mixed = output / "sparse-mixed.wgsl"
    mixed.write_text("@group(0) @binding(1) var<storage, read> a: array<u32, 8>;\n"
                     "@group(0) @binding(7) var<storage, read_write> b: array<u32, 8>;\n"
                     "@compute @workgroup_size(8) fn main(@builtin(global_invocation_id) gid: vec3u) { b[gid.x] = a[gid.x] + gid.x; }\n", encoding="utf-8")
    receipt["shaders"][str(mixed)] = digest(mixed)
    mixed_command = dispatch(mixed, handle=82)
    mixed_command["bindings"][0]["binding"] = 7
    read_binding = copy.deepcopy(mixed_command["bindings"][0])
    read_binding.update(binding=1, resource_handle=81, buffer_type="read_only_storage")
    mixed_command["bindings"].append(read_binding)
    expected = [value+i for i, value in enumerate(initial)]
    mixed_checked = sparse_oracle(mixed_command, 7, expected)
    reordered = copy.deepcopy(mixed_checked)
    reordered["bindings"].reverse()
    run("sparse-mixed-order-reuse", [write(initial, handle=81), mixed_checked, reordered], ["ok"]*3, 2)
    outside = copy.deepcopy(mixed_command)
    outside["bindings"][0]["binding"] = 8
    missing = copy.deepcopy(mixed_command)
    missing["bindings"][1]["binding"] = 2
    aliased = copy.deepcopy(mixed_command)
    aliased["bindings"][0]["resource_handle"] = 81
    # Oracle replay requires every graph command to succeed. Keep rejected-operation
    # status/submission recovery separate from the exact-output graphs above.
    run("sparse-rejection-recovery", [write(initial, handle=81), mixed_command, outside, missing, aliased, mixed_command],
        ["ok", "ok", "unsupported", "unsupported", "unsupported", "ok"])
    print("Physical CLI storage acceptance complete; no paired speed claim.", flush=True)


if __name__ == "__main__":
    main()
