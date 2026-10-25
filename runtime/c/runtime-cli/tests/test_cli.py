#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""doe-c-runtime on the selected Vulkan adapter: example streams execute, their
trace artifacts pass pipeline/trace/replay.py, the row hash chain equals the
doe-zig-runtime rows for the same streams, an output oracle matches its
independent reference digest, copies and texture writes reproduce bytes
computed independently on the host, and unsupported arguments, commands, layouts
and oracle mismatches fail with a nonzero exit. The adapter that ran is printed from the
trace meta's adapterInfo; a CPU (fallback) adapter is not GPU evidence. Every
row and meta validates against config/trace.schema.json and
config/trace-meta.schema.json. Semantic dispatches
write operator records, repro bundles and captures equal to doe-zig-runtime's
outside the execution fields, with captures checked against the host oracle;
a dispatch doe-zig-runtime would evaluate for numeric stability is refused.
Plan mode rows chain like doe-plan-executor's and its loads and dispatches
read back as the host computes them."""

from __future__ import annotations

import argparse
import hashlib
import json

import jsonschema
import os
import struct
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
EXAMPLES = REPO / "examples"
KERNELS = REPO / "bench" / "kernels"
REPLAY = REPO / "pipeline" / "trace" / "replay.py"
sys.path.insert(0, str(REPO / "bench" / "oracles"))
import workgroup_recurrence_u32_v1 as recurrence  # noqa: E402  (independent CPU reference)

# Final row hashes doe-zig-runtime wrote for these streams (db0979828, lavapipe).
ZIG_FINAL_HASH = {
    "p0_resource_lifecycle": "0xb51cc5ee8f6a8133",
    "rmsnorm_tree": "0x3ab94f41b34d8349",
    "workgroup_atomic": "0xd00d567969d44b72",
}
# doe-zig-runtime (db0979828, --quirk-mode off, lavapipe) final hashes for copy
# streams; its native Vulkan lane reports the copies "unsupported" while the row
# hash chain covers command identity, so the chains must still be equal.
ZIG_COPY_FINAL_HASH = {
    "copy_buffer_to_buffer_4kb": "0xdb020b52b2155989",
    "copy_buffer_to_buffer_overlap": "0xdb020b52b2155989",
    "copy_buffer_to_texture": "0xdb020b52b2155989",
    "copy_texture_to_texture": "0xdb020b52b2155989",
    "copy_texture_to_texture_stress": "0x56fc0e97e927c76a",
}
ZIG_ROUNDTRIP_FINAL_HASH = "0x58730f9067dc7873"
# doe-zig-runtime (same build, --quirk-mode off, lavapipe) final hashes for
# render streams, which its native Vulkan lane executes.
ZIG_RENDER_FINAL_HASH = {
    "draw_call_proxy": "0x509d940f0c58349f",
    "draw_call_indexed_proxy": "0x509d940f0c58349f",
    "draw_call_state_bindings": "0x509d940f0c58349f",
    "render_pass_dynamic_viewport_scissor": "0xfdc0c9aab552cb39",
    "texture_sampler_write_query_destroy": "0xe5677bd4a524aa50",
    "uniform_buffer_update_writebuffer_partial_single": "0x427ca82f073f3b6c",
}
ZIG_RENDER_ROUNDTRIP_FINAL_HASH = "0x2f3624a9819ae816"
PROXY_COLOR = (64, 128, 191, 255)  # draw_call_proxy_fragment.wgsl (0.25, 0.5, 0.75, 1) in rgba8unorm
CLEAR_COLOR = (0, 0, 0, 255)


def render_roundtrip_stream() -> tuple[list, dict[int, tuple[int, int, int, int]]]:
    """Proxy draws into named 16x16 targets, one scissored to x 2..9, y 1..4
    from the top and one indexed and unscissored, each copied to a buffer."""
    def target(handle: int) -> dict:
        return {"kind": "texture_write", "textureHandle": handle, "width": 16, "height": 16, "format": "rgba8unorm",
                "usage": 23}

    def readback(src: int, dst: int) -> dict:
        return {"kind": "copy_texture_to_buffer", "bytes": 4096, "src_handle": src, "src_width": 16, "src_height": 16,
                "src_format": "rgba8unorm", "src_usage": 23, "dst_handle": dst, "dst_bytes_per_row": 256,
                "dst_rows_per_image": 16}

    stream = [
        target(700),
        {"kind": "render_draw", "drawCount": 3, "targetHandle": 700, "targetWidth": 16, "targetHeight": 16,
         "targetFormat": "rgba8unorm", "scissorX": 2, "scissorY": 1, "scissorWidth": 8, "scissorHeight": 4},
        readback(700, 701),
        target(702),
        {"kind": "draw_indexed", "drawCount": 2, "indexData": [0, 1, 2], "indexFormat": "uint32", "targetHandle": 702,
         "targetWidth": 16, "targetHeight": 16, "targetFormat": "rgba8unorm", "encodeMode": "render-pass"},
        readback(702, 703),
    ]
    return stream, {701: (2, 1, 8, 4), 703: (0, 0, 16, 16)}


def pixels_match(data: bytes, scissor: tuple[int, int, int, int]) -> bool:
    """Every texel is the proxy color inside the scissor and the clear color
    outside; channels may differ by one unit of unorm rounding."""
    x0, y0, width, height = scissor
    for y in range(16):
        for x in range(16):
            texel = data[y * 256 + x * 4:y * 256 + x * 4 + 4]
            inside = x0 <= x < x0 + width and y0 <= y < y0 + height
            expected = PROXY_COLOR if inside else CLEAR_COLOR
            if any(abs(a - b) > 1 for a, b in zip(texel, expected)):
                return False
    return True


def roundtrip_stream() -> tuple[list, dict[int, bytes]]:
    """Buffer and texture copies in every direction plus a texture write, and the
    bytes each read-back buffer must hold, computed here without the GPU."""
    words = [(i * 2654435761 + 12345) & 0xFFFFFFFF for i in range(1024)]
    source = b"".join(struct.pack("<I", word) for word in words)
    texel = {"width": 64, "height": 16, "format": "rgba8unorm", "usage": 6}
    small = [(i * 17) % 256 for i in range(16)]

    def side(prefix: str) -> dict:
        return {f"{prefix}_{key}": value for key, value in texel.items()}

    stream = [
        {"kind": "buffer_write", "handle": 500, "data": words},
        {"kind": "copy_buffer_to_buffer", "bytes": 2048, "src_handle": 500, "src_offset": 1024,
         "dst_handle": 501, "dst_offset": 512},
        {"kind": "copy_buffer_to_texture", "bytes": 4096, "src_handle": 500, "dst_handle": 600, **side("dst"),
         "dst_bytes_per_row": 256, "dst_rows_per_image": 16},
        # Row pitches matter only when a quirk stages the copy through a buffer.
        {"kind": "copy_texture_to_texture", "bytes": 4096, "src_handle": 600, **side("src"), "src_bytes_per_row": 256,
         "src_rows_per_image": 16, "dst_handle": 601, **side("dst"), "dst_bytes_per_row": 256, "dst_rows_per_image": 16},
        {"kind": "copy_texture_to_buffer", "bytes": 4096, "src_handle": 601, **side("src"), "dst_handle": 502,
         "dst_bytes_per_row": 256, "dst_rows_per_image": 16},
        {"kind": "texture_write", "textureHandle": 602, "width": 2, "height": 2, "format": "rgba8unorm",
         "bytesPerRow": 8, "rowsPerImage": 2, "data": small},
        {"kind": "texture_query", "handle": 602, "width": 2, "height": 2, "depthOrArrayLayers": 1,
         "format": "rgba8unorm", "dimension": "2d", "sampleCount": 1, "usage": 6},
        {"kind": "copy_texture_to_buffer", "bytes": 512, "src_handle": 602, "src_width": 2, "src_height": 2,
         "src_format": "rgba8unorm", "src_usage": 6, "dst_handle": 503, "dst_bytes_per_row": 256,
         "dst_rows_per_image": 2},
        {"kind": "texture_destroy", "handle": 602},
    ]
    padded = bytearray(512)
    padded[0:8] = bytes(small[0:8])
    padded[256:264] = bytes(small[8:16])
    return stream, {501: bytes(512) + source[1024:3072], 502: source, 503: bytes(padded)}


def run(binary: Path, work: Path, name: str, *extra: str, commands: Path | None = None) -> tuple[int, str, dict, list]:
    jsonl = work / f"{name}.jsonl"
    meta = work / f"{name}.meta.json"
    for path in (jsonl, meta):
        if path.exists():
            path.unlink()
    stream = commands or EXAMPLES / f"{name}_commands.json"
    argv = [str(binary), "--commands", str(stream), "--execute", "--backend", "native",
            "--kernel-root", str(KERNELS), "--trace-jsonl", str(jsonl), "--trace-meta", str(meta), *extra]
    result = subprocess.run(argv, capture_output=True, text=True, timeout=900, cwd=REPO)
    meta_doc = json.loads(meta.read_text()) if meta.exists() else {}
    rows = [json.loads(line) for line in jsonl.read_text().splitlines() if line.strip()] if jsonl.exists() else []
    check_schemas(name, meta_doc, rows)
    return result.returncode, result.stderr, meta_doc, rows


def replay_passes(work: Path, name: str) -> bool:
    result = subprocess.run([sys.executable, str(REPLAY), "--trace-jsonl", str(work / f"{name}.jsonl"),
                             "--trace-meta", str(work / f"{name}.meta.json")], capture_output=True, text=True)
    return result.returncode == 0 and result.stdout.strip() == "PASS"


def _validator(name: str):
    schema = json.loads((REPO / "config" / name).read_text())
    return jsonschema.validators.validator_for(schema)(schema)


ROW_SCHEMA = _validator("trace.schema.json")
META_SCHEMA = _validator("trace-meta.schema.json")


def check_schemas(label: str, meta: dict, rows: list) -> None:
    """The bench-facing trace contracts hold for every artifact this CLI writes."""
    errors = [f"meta: {e.message}" for e in META_SCHEMA.iter_errors(meta)] if meta else []
    for row in rows:
        errors += [f"row {row.get('seq')}: {e.message}" for e in ROW_SCHEMA.iter_errors(row)]
    check(not errors, f"{label} violates the trace schemas: {errors[:3]}")


def check(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


def check_copies(binary: Path, work: Path) -> None:
    for name, final_hash in ZIG_COPY_FINAL_HASH.items():
        code, stderr, meta, rows = run(binary, work, name)
        check(code == 0 and meta["hash"] == final_hash and all(r["executionStatus"] == "ok" for r in rows),
              f"{name}: exit {code}, hash {meta.get('hash')} != {final_hash}: {stderr}")
        check(replay_passes(work, name), f"{name} artifacts fail replay")
    stream, expected = roundtrip_stream()
    commands = work / "roundtrip_commands.json"
    commands.write_text(json.dumps(stream))
    readbacks = [arg for handle in expected for arg in ("--readback-buffer", f"{handle}={work / f'readback_{handle}.bin'}")]
    code, stderr, meta, rows = run(binary, work, "roundtrip", *readbacks, commands=commands)
    check(code == 0 and meta["hash"] == ZIG_ROUNDTRIP_FINAL_HASH, f"roundtrip: exit {code} hash {meta.get('hash')}: {stderr}")
    check(replay_passes(work, "roundtrip"), "roundtrip artifacts fail replay")
    for handle, data in expected.items():
        check((work / f"readback_{handle}.bin").read_bytes() == data, f"buffer {handle} differs from the host reference")
    # WebGPU layout rules hold: a 128-byte row pitch is not a valid buffer-texture copy.
    code, stderr, _, rows = run(binary, work, "copy_buffer_to_texture_tight_pitch")
    check(code == 1 and rows and rows[0]["executionStatus"] == "error", f"tight pitch: exit {code}: {stderr}")
    code, stderr, _, _ = run(binary, work, "copy_texture_to_texture_subresource")
    check(code == 2 and "mip_level" in stderr, f"mip level copy: exit {code}: {stderr}")
    mismatch = work / "query_mismatch_commands.json"
    mismatch.write_text(json.dumps([stream[5], {**stream[6], "width": 3}]))
    code, stderr, _, _ = run(binary, work, "query_mismatch", commands=mismatch)
    check(code == 1 and "texture_query_mismatch" in stderr, f"query mismatch: exit {code}: {stderr}")
    code, stderr, _, _ = run(binary, work, "roundtrip", "--readback-buffer", f"999={work / 'missing.bin'}", commands=commands)
    check(code == 1 and "readback_missing" in stderr, f"missing readback: exit {code}: {stderr}")


# doe-zig-runtime (same build, default trace mode and profile, lavapipe) final
# hashes with these quirk files: the rows carry the matched quirk, its score and
# fields, so equal chains mean equal decisions.
ZIG_QUIRK_FINAL_HASH = {
    ("draw_call_proxy", "intel_gen12_temp_buffer.json", ()): "0x2d0cbb3f22a29608",
    ("texture_sampler_write_query_destroy", "intel_gen12_temp_buffer_list.json", ()): "0x5f84e816f6ce4176",
    ("p0_resource_lifecycle", "mined_toggle_sample.json", ("--vendor", "amd")): "0x8d15b26a038385dc",
}
ZIG_QUIRK_ROUNDTRIP_FINAL_HASH = "0xb30af1036450c428"  # copy roundtrip, intel_gen12_temp_buffer.json


def check_quirks(binary: Path, work: Path) -> None:
    for (name, quirks, extra), final_hash in ZIG_QUIRK_FINAL_HASH.items():
        code, stderr, meta, rows = run(binary, work, name, "--quirks", str(EXAMPLES / "quirks" / quirks), *extra)
        check(code == 0 and meta["hash"] == final_hash and meta["quirkMode"] == "trace" and meta["matchedCount"] == len(rows),
              f"{name} with {quirks}: exit {code}, hash {meta.get('hash')} != {final_hash}: {stderr}")
        check(replay_passes(work, name), f"{name} with {quirks}: artifacts fail replay")
    # Active mode stages buffer-to-texture and texture-to-texture copies through a
    # temporary buffer; the read-back bytes are unchanged and the chain still equals Zig's.
    stream, expected = roundtrip_stream()
    commands = work / "roundtrip_commands.json"
    commands.write_text(json.dumps(stream))
    readbacks = [arg for handle in expected for arg in ("--readback-buffer", f"{handle}={work / f'staged_{handle}.bin'}")]
    code, stderr, meta, rows = run(binary, work, "staged", "--quirks", str(EXAMPLES / "quirks" / "intel_gen12_temp_buffer.json"),
                                   "--quirk-mode", "active", *readbacks, commands=commands)
    check(code == 0 and meta["hash"] == ZIG_QUIRK_ROUNDTRIP_FINAL_HASH and meta["quirkMode"] == "active",
          f"active staging: exit {code} hash {meta.get('hash')}: {stderr}")
    check(rows[2]["action"] == "use_temporary_buffer", "the staged copy row names its action")
    for handle, data in expected.items():
        check((work / f"staged_{handle}.bin").read_bytes() == data, f"staged buffer {handle} differs from the host reference")
    # An action this runtime cannot apply is refused in active mode, never ignored.
    unappliable = json.loads((EXAMPLES / "quirks" / "intel_gen12_temp_buffer.json").read_text())
    unappliable["action"] = {"kind": "use_temporary_render_texture", "params": {"minMipLevel": 2}}
    path = work / "render_texture_quirk.json"
    path.write_text(json.dumps(unappliable))
    code, stderr, _, _ = run(binary, work, "draw_call_proxy", "--quirks", str(path), "--quirk-mode", "active")
    check(code == 2 and "cannot be applied" in stderr, f"unappliable quirk: exit {code}: {stderr}")
    unappliable["action"] = {"kind": "rewrite"}
    path.write_text(json.dumps(unappliable))
    code, stderr, _, _ = run(binary, work, "draw_call_proxy", "--quirks", str(path))
    check(code == 2 and "not supported" in stderr, f"unknown quirk action: exit {code}: {stderr}")


def check_render(binary: Path, work: Path) -> None:
    for name, final_hash in ZIG_RENDER_FINAL_HASH.items():
        code, stderr, meta, rows = run(binary, work, name)
        check(code == 0 and meta["hash"] == final_hash and all(r["executionStatus"] == "ok" for r in rows),
              f"{name}: exit {code}, hash {meta.get('hash')} != {final_hash}: {stderr}")
        check(replay_passes(work, name), f"{name} artifacts fail replay")
    proxy = json.loads((EXAMPLES / "draw_call_proxy_commands.json").read_text())[0]
    _, _, meta, rows = run(binary, work, "draw_call_proxy")
    check(rows[0]["executionDispatchCount"] == proxy["drawCount"], "render dispatch count is the draw count")
    stream, scissors = render_roundtrip_stream()
    commands = work / "render_roundtrip_commands.json"
    commands.write_text(json.dumps(stream))
    readbacks = [arg for handle in scissors for arg in ("--readback-buffer", f"{handle}={work / f'render_{handle}.bin'}")]
    code, stderr, meta, _ = run(binary, work, "render_roundtrip", *readbacks, commands=commands)
    check(code == 0 and meta["hash"] == ZIG_RENDER_ROUNDTRIP_FINAL_HASH,
          f"render roundtrip: exit {code} hash {meta.get('hash')}: {stderr}")
    for handle, scissor in scissors.items():
        check(pixels_match((work / f"render_{handle}.bin").read_bytes(), scissor), f"render target {handle} pixels")
    missing = work / "render_missing_commands.json"
    missing.write_text(json.dumps([{**stream[1], "targetHandle": 799}]))
    code, stderr, _, _ = run(binary, work, "render_missing", commands=missing)
    check(code == 1 and "render_target_missing" in stderr, f"missing render target: exit {code}: {stderr}")


# doe-zig-runtime (built from 977122f33's runtime/zig, --quirk-mode off, lavapipe,
# 2026-10-25) rows for semantic_stream(); its operator records, repro bundles and
# captures are the reference for the structure checked below.
ZIG_SEMANTIC_HASHES = ["0x3eb0c370aa46f5f9", "0x4124865a91c3cc65", "0x39032e82e4daa181", "0xf48294d320b4db17",
                       "0xeaf883dd6c6b0c40", "0x7c3076098b52e104"]
# doe-zig-runtime trace-only rows (no --execute) for numeric-stability-decode-greedy.
ZIG_GREEDY_HASHES = ["0x3eb0c370aa46f5f9", "0x92f65df8db1ad54a", "0xe545cdbfbc0063fa", "0x66766d878bdd7db",
                     "0xf570349e8eb94e1b", "0xf79bde625797c784"]


def semantic_stream() -> list:
    """workgroup_atomic dispatches with every semantic field in both spellings,
    in-range captures, one without stage or phase, one without semantic identity
    and an out-of-range capture."""
    base = json.loads((EXAMPLES / "workgroup_atomic_commands.json").read_text())[0]
    base.pop("output_oracle")
    base["warmup_dispatch_count"] = 0

    def dispatch(repeat: int, **fields: object) -> dict:
        return {**base, "repeat": repeat, **fields}

    only = {"semanticOpId": "test.only_op"}
    return [
        {"kind": "buffer_write", "handle": 7000, "bufferSize": 16, "data": [1, 2, 3, 4]},
        dispatch(4, semanticOpId="test.atomic_accumulate", semanticStage="c_parity", semanticPhase="accumulate",
                 semanticTokenIndex=3, semanticLayerIndex=1, semanticExecutionPlanHash='plan-"q"-1',
                 captureBufferHandle=5001, captureOffset=64, captureSize=256),
        dispatch(2, semantic_op_id="test.atomic_again", semantic_stage="c_parity", semantic_phase="again",
                 semantic_token_index=0, capture_buffer_handle=5001, capture_size=16),
        dispatch(1, **only),
        dispatch(1),
        dispatch(1, **only, captureBufferHandle=5001, captureOffset=1048570, captureSize=16),
    ]


def check_semantic(binary: Path, work: Path) -> None:
    commands = work / "semantic_commands.json"
    commands.write_text(json.dumps(semantic_stream()))
    code, stderr, meta, rows = run(binary, work, "semantic", "--quirk-mode", "off", commands=commands)
    check(code == 0 and [row["hash"] for row in rows] == ZIG_SEMANTIC_HASHES, f"semantic rows: exit {code}: {stderr}")
    check(replay_passes(work, "semantic"), "semantic artifacts fail replay")
    check(rows[1]["semanticLayerIndex"] == 1 and rows[1]["semanticExecutionPlanHash"] == 'plan-"q"-1'
          and list(rows[1])[9:15] == ["semanticOpId", "semanticStage", "semanticPhase", "semanticTokenIndex",
                                      "semanticLayerIndex", "semanticExecutionPlanHash"]
          and not any(key.startswith("semantic") for key in rows[4]), "semantic row fields and order")
    anchor = work / "semantic.meta.json"
    manifest = Path(f"{anchor}.operators.json")
    check(meta["semanticTracingEnabled"] is True and meta["semanticOpRowCount"] == 4 and meta["semanticCaptureCount"] == 2
          and meta["semanticReproCount"] == 4 and meta["operatorRecordManifestPath"] == str(manifest)
          and meta["operatorRecordManifestHash"] == hashlib.sha256(manifest.read_bytes()).hexdigest(), "semantic meta")
    records = json.loads(manifest.read_text())
    check([r["sourceIndex"] for r in records] == [1, 2, 3, 5]
          and [r["trace"]["hash"] for r in records] == [ZIG_SEMANTIC_HASHES[i] for i in (1, 2, 3, 5)]
          and all(r["trace"]["traceMetaPath"] == str(anchor) and r["shaderArtifacts"] == {} for r in records)
          and records[0]["commandShape"] == {"dispatchGeometry": {"x": 1024, "y": 1, "z": 1}, "repeat": 4,
                                             "bindingCount": 1, "bindingBufferBytes": 1048576}
          and records[2]["capture"] is None
          and records[3]["capture"] == {"bufferHandle": 5001, "offset": 1048570, "size": 16, "status": "error",
                                        "errorCode": "InvalidArgument"}, "operator records")
    # Captures hold the buffer after the dispatches so far, computed on the host.
    for record, dispatches, start, size in ((records[0], 4, 64, 256), (records[1], 6, 0, 16)):
        data = Path(record["capture"]["path"]).read_bytes()
        check(data == recurrence.expected_output(dispatch_count=dispatches)[start:start + size]
              and record["capture"]["sha256"] == hashlib.sha256(data).hexdigest(), f"capture {record['sourceIndex']}")
    repro = Path(records[0]["repro"]["commandsPath"]).read_text()
    check(repro == '[\n{"seq":0,"kind":"kernel_dispatch","kernel":"workgroup_atomic.wgsl","x":1024,"y":1,"z":1,"repeat":4,'
          '"repeatSynchronization":"dependent","semanticOpId":"test.atomic_accumulate","semanticStage":"c_parity",'
          '"semanticPhase":"accumulate","semanticTokenIndex":3,"semanticLayerIndex":1,'
          '"semanticExecutionPlanHash":"plan-\\"q\\"-1","captureBufferHandle":5001,"captureOffset":64,"captureSize":256}\n]\n',
          "repro commands bytes equal doe-zig-runtime's")
    repro_meta = json.loads(Path(records[0]["repro"]["metaPath"]).read_text())
    check(repro_meta["commandsPath"] == records[0]["repro"]["commandsPath"] and repro_meta["bitwise"] is False
          and repro_meta["commandShape"] == records[0]["commandShape"], "repro meta")
    # doe-zig-runtime evaluates decode.final_logits on matmul-gemv.wgsl for numeric
    # stability when it writes a trace meta; this runtime refuses that stream.
    greedy = EXAMPLES / "numeric-stability-decode-greedy.commands.json"
    code, stderr, _, _ = run(binary, work, "greedy", "--quirk-mode", "off", commands=greedy)
    check(code == 2 and "command 3: numeric stability evaluation" in stderr, f"numeric stability refusal: {code} {stderr}")
    # Without a trace meta nothing is evaluated: the rows chain like doe-zig-runtime's
    # and records anchor at the trace JSONL.
    jsonl = work / "greedy_rows.jsonl"
    result = subprocess.run([str(binary), "--commands", str(greedy), "--execute", "--quirk-mode", "off",
                             "--trace-jsonl", str(jsonl)], capture_output=True, text=True, timeout=900, cwd=REPO)
    greedy_records = json.loads(Path(f"{jsonl}.operators.json").read_text())
    check(result.returncode == 0 and [json.loads(line)["hash"] for line in jsonl.read_text().splitlines()] == ZIG_GREEDY_HASHES
          and [r["semanticOpId"] for r in greedy_records] == ["decode.final_logits", "decode.sample_token"]
          and all("traceMetaPath" not in r["trace"] for r in greedy_records), f"greedy rows: {result.stderr}")
    code, stderr, _, _ = run(binary, work, "greedy", "--numeric-stability-policy", str(work / "missing-policy.json"),
                             commands=greedy)
    check(code == 2 and "cannot read" in stderr, f"missing numeric stability policy: {code} {stderr}")


# doe-plan-executor --dry-run (built from 977122f33's runtime/zig, 2026-10-25)
# rows for plan_document(); the payload hash does not depend on execution.
ZIG_PLAN_HASHES = ["0xa404173999673bb2", "0xfcb2c70c64d42c35", "0x2b6f0590e0ed85bb", "0x12f929dc81210bb3"]


def plan_document() -> dict:
    """A buffer_write, a buffer_load from the asset cache and two workgroup_atomic
    dispatches, the second carrying compatibility-stream fields the plan supersedes."""
    atomic = {"kind": "kernel_dispatch", "kernel": "workgroup_atomic.wgsl", "x": 1024, "y": 1, "z": 1,
              "initialize_buffers_on_create": True,
              "bindings": [{"binding": 0, "group": 0, "kind": "buffer", "buffer_type": "storage",
                            "resource_handle": 5001, "buffer_size": 1048576, "visibility": "compute"}]}
    commands = [
        {"kind": "buffer_write", "handle": 7000, "bufferSize": 16, "data": [1, 2, 3, 4]},
        {"kind": "buffer_load", "handle": 7001, "offset": 0, "bufferSize": 64, "byteLength": 64,
         "cacheNamespace": "c_parity", "cacheKey": "words16", "assetKey": "c_parity.words16",
         "generator": "splitmix64_f32_nonzero_v1", "seed": 7, "scale": 1.0},
        atomic,
        dict(atomic, semanticOpId="ignored.by.plan", captureBufferHandle=5001, captureSize=16),
    ]
    return {"schemaVersion": 1, "planKind": "c_parity", "workloadId": "c_plan_parity",
            "irPath": "bench/ir/c-plan-parity-missing.json", "irScenario": "c_plan_parity",
            "description": "C plan-mode parity", "planPath": "c_plan_parity.plan.json",
            "commandsPath": "c_plan_parity_commands.json", "commandCount": 4, "bufferWriteCount": 1,
            "bufferLoadCount": 1, "dispatchCount": 2, "sourceIrSha256": "0" * 64,
            "compatibilityCommandsSha256": "1" * 64, "commands": commands, "planSha256": "ab" * 32}


def run_plan(binary: Path, work: Path, plan: dict, *extra: str, workload: str = "c_plan_parity"):
    path = work / "plan.json"
    path.write_text(json.dumps(plan))
    jsonl, meta = work / "plan.jsonl", work / "plan.meta.json"
    for stale in (jsonl, meta):
        if stale.exists():
            stale.unlink()
    env = {**os.environ, "DOE_BENCH_ASSET_CACHE_DIR": str(work / "assets")}
    result = subprocess.run([str(binary), "--plan", str(path), "--workload", workload, "--trace-jsonl", str(jsonl),
                             "--trace-meta", str(meta), "--kernel-root", str(KERNELS), *extra],
                            capture_output=True, text=True, timeout=900, cwd=REPO, env=env)
    rows = [json.loads(line) for line in jsonl.read_text().splitlines()] if jsonl.exists() else []
    meta_doc = json.loads(meta.read_text()) if meta.exists() else {}
    check_schemas(f"plan {workload}", meta_doc, rows)
    return result, meta_doc, rows


def check_plan(binary: Path, work: Path) -> None:
    asset = bytes((i * 37 + 11) % 256 for i in range(64))
    (work / "assets" / "c_parity").mkdir(parents=True, exist_ok=True)
    (work / "assets" / "c_parity" / "words16.bin").write_bytes(asset)
    readbacks = ["--readback-buffer", f"7001={work / 'plan_7001.bin'}", "--readback-buffer", f"5001={work / 'plan_5001.bin'}"]
    result, meta, rows = run_plan(binary, work, plan_document(), *readbacks)
    check(result.returncode == 0 and [row["hash"] for row in rows] == ZIG_PLAN_HASHES, f"plan rows: {result.stderr}")
    check([row["opCode"] for row in rows] == ["buffer_write", "buffer_load", "kernel_dispatch", "kernel_dispatch"]
          and rows[1]["semanticOpId"] == "step-000001" and rows[3]["semanticOpId"] == "step-000003"
          and all(row["semanticStage"] == "runtime_plan" and row["semanticExecutionPlanHash"] == "ab" * 32 for row in rows)
          and "matched" not in rows[0] and all(row["executionStatus"] == "ok" for row in rows), "plan row layout")
    check(meta["hostPlanArtifactHash"] == "ab" * 32 and meta["hostPlanArtifactPath"] == str(work / "plan.json")
          and meta["quirkMode"] == "off" and meta["hostKernelPrewarmTotalNs"] == 0 and meta["semanticOpRowCount"] == 0
          and not (work / "plan.meta.json.operators.json").exists(), "plan meta")
    check("1 kernel_dispatch steps (first: command 3)" in result.stderr, "superseded compatibility fields are reported")
    check((work / "plan_7001.bin").read_bytes() == asset
          and (work / "plan_5001.bin").read_bytes() == recurrence.expected_output(dispatch_count=2),
          "plan load and dispatches read back as computed on the host")
    refusals = [
        (plan_document(), ("--quirks", str(EXAMPLES / "quirks" / "intel_gen12_temp_buffer.json")), "c_plan_parity", "takes no"),
        (plan_document(), (), "another_workload", "is not --workload"),
        ({**plan_document(), "dispatchCount": 3}, (), "c_plan_parity", "does not equal"),
        ({**plan_document(), "extra": 1}, (), "c_plan_parity", "\"extra\""),
        ({**plan_document(), "commands": [{**plan_document()["commands"][1], "cacheKey": "missing"}],
          "commandCount": 1, "bufferWriteCount": 0, "dispatchCount": 0}, (), "c_plan_parity", "cannot be read"),
    ]
    for plan, extra, workload, needle in refusals:
        result, _, _ = run_plan(binary, work, plan, *extra, workload=workload)
        check(result.returncode == 2 and needle in result.stderr, f"plan refusal {needle}: {result.returncode} {result.stderr}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)

    adapter = None
    for name, final_hash in ZIG_FINAL_HASH.items():
        code, stderr, meta, rows = run(args.binary, args.work, name, "--validate-output-oracles")
        check(code == 0, f"{name} exited {code}: {stderr}")
        check(replay_passes(args.work, name), f"{name} artifacts fail pipeline/trace/replay.py")
        check(meta["hash"] == final_hash, f"{name} final hash {meta['hash']} != doe-zig-runtime {final_hash}")
        check(all(row["executionStatus"] == "ok" for row in rows), f"{name} has a failed row")
        check(meta["executionSuccessCount"] == len(rows) == meta["commandCount"], f"{name} counts")
        # Bench comparability reads these; a fixed backend has no lane policy hash.
        check(meta["backendId"] == meta["executionBackend"] == "doe_c_vulkan" and meta["fallbackUsed"] is False
              and meta["backendSelectionReason"] == "fixed_backend" and "selectionPolicyHash" not in meta
              and "uploadBufferUsage" not in meta and "uploadSubmitEvery" not in meta,
              f"{name} backend meta (no upload fields, as doe-zig-runtime)")
        adapter = meta["adapterInfo"]
        check(adapter["vendorID"] > 0 and isinstance(adapter["driverVersion"], int) and adapter["device"],
              f"{name} adapterInfo names the executing adapter")
    rmsnorm = json.loads((args.work / "rmsnorm_tree.meta.json").read_text())
    check(rmsnorm["executionDispatchCount"] == 100 and rmsnorm["executionSubmitCount"] == 2, "rmsnorm dispatch/submit counts")
    # Meta fields the bench reads, as doe-zig-runtime writes them: kernel prewarm
    # outside execution timing, no persistent pipeline cache, an empty operator manifest.
    check(rmsnorm["hostKernelPrewarmTotalNs"] > 0 and rmsnorm["pipelineCache"]["state"] == "disabled", "rmsnorm prewarm and cache")
    check(json.loads((args.work / "rmsnorm_tree.meta.json.operators.json").read_text()) == [], "operator manifest")
    atomic = json.loads((args.work / "workgroup_atomic.meta.json").read_text())
    check(atomic["outputOracleCount"] == 1 and atomic["outputOracleMatchedCount"] == 1, "workgroup_atomic oracle did not match")
    check(atomic["outputOracleActualSha256"] == atomic["outputOracleExpectedSha256"], "oracle digests differ")
    check(atomic["outputOracleReferenceClass"] == "independent_v1" and atomic["outputOracleMismatchCount"] == 0,
          "oracle reference class and exact-digest counts")

    # Alias kinds label rows with the normalized name, as doe-zig-runtime does.
    aliases = args.work / "alias_commands.json"
    aliases.write_text(json.dumps([{"kind": "write_buffer", "handle": 1, "data": [1, 2, 3, 4]},
                                   {"kind": "buffer_upload", "bytes": 16}]))
    code, stderr, meta, rows = run(args.binary, args.work, "alias", commands=aliases)
    check(code == 0 and [row["command"] for row in rows] == ["buffer_write", "upload"]
          and [row["hash"] for row in rows] == ["0x3eb0c370aa46f5f9", "0x7ac9433e64554c12"], f"alias rows: {code} {stderr}")

    # --command-repeat continues one hash chain over the replayed stream.
    code, stderr, meta, rows = run(args.binary, args.work, "p0_resource_lifecycle", "--command-repeat", "2")
    check(code == 0 and len(rows) == 10 and meta["commandCount"] == 10, f"command repeat: {code} {stderr}")
    check(replay_passes(args.work, "p0_resource_lifecycle"), "command repeat artifacts fail replay")

    # A wrong expected digest is a failure, not a pass.
    stream = json.loads((EXAMPLES / "workgroup_atomic_commands.json").read_text())
    stream[0]["output_oracle"]["expected_sha256"] = "0" * 64
    mismatch = args.work / "oracle_mismatch_commands.json"
    mismatch.write_text(json.dumps(stream))
    code, stderr, meta, _ = run(args.binary, args.work, "oracle_mismatch", "--validate-output-oracles", commands=mismatch)
    check(code == 1 and meta["outputOracleFailedCount"] == 1 and "mismatch" in stderr, f"oracle mismatch: {code} {stderr}")

    # Unsupported input fails before device work with exit code 2.
    rejected = [
        (["--quirk-mode", "on"], "off, trace or active"),
        (["--driver", "31.x"], "major[.minor[.patch]]"),
        (["--queue-sync-mode", "deferred"], "per-command"),
        (["--replay", "x"], "not implemented"),
        (["--gpu-timestamp-mode", "require"], "GPU timestamps"),
        (["--numeric-stability-execution-profile", "x"], "not implemented"),
    ]
    for extra, needle in rejected:
        code, stderr, _, _ = run(args.binary, args.work, "rmsnorm_tree", *extra)
        check(code == 2 and needle in stderr, f"{extra}: exit {code}: {stderr}")
    code, stderr, _, _ = run(args.binary, args.work, "dispatch", commands=EXAMPLES / "dispatch_workgroups_commands.json")
    check(code == 2 and "not implemented" in stderr, f"dispatch stream: exit {code}: {stderr}")

    check_copies(args.binary, args.work)
    check_render(args.binary, args.work)
    check_quirks(args.binary, args.work)
    check_semantic(args.binary, args.work)
    check_plan(args.binary, args.work)

    print(f"PASS doe-c-runtime: 3 example streams with doe-zig-runtime hash chains, replay-valid artifacts, "
          f"an independent oracle match, alias labels, command repeat, an oracle mismatch, 7 rejections, "
          f"{len(ZIG_COPY_FINAL_HASH)} copy streams, a copy/texture roundtrip read back against host bytes, 4 copy "
          f"failures, {len(ZIG_RENDER_FINAL_HASH)} render streams, scissored and indexed draws read back against the "
          f"proxy and clear colors, a missing render target, {len(ZIG_QUIRK_FINAL_HASH)} quirk-matched streams, active "
          f"staging read back against host bytes, 2 quirk refusals, semantic rows with operator records, repro "
          f"bundles and host-checked captures, 2 numeric stability refusals, plan rows chained like "
          f"doe-plan-executor's with host-checked loads and dispatches and 5 plan refusals on "
          f"{adapter.get('device')} ({adapter.get('adapterType')}, {adapter.get('backendType')}); every trace row and "
          f"meta validates against config/trace.schema.json and config/trace-meta.schema.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
