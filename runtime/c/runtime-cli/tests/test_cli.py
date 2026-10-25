#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""doe-c-runtime on the selected Vulkan adapter: example streams execute, their
trace artifacts pass pipeline/trace/replay.py, the row hash chain equals the
doe-zig-runtime rows for the same streams, an output oracle matches its
independent reference digest, copies and texture writes reproduce bytes
computed independently on the host, and unsupported arguments, commands, layouts
and oracle mismatches fail with a nonzero exit. The adapter that ran is printed from the
trace meta; a CPU (fallback) adapter is not GPU evidence."""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
EXAMPLES = REPO / "examples"
KERNELS = REPO / "bench" / "kernels"
REPLAY = REPO / "pipeline" / "trace" / "replay.py"

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
        {"kind": "copy_texture_to_texture", "bytes": 4096, "src_handle": 600, **side("src"), "dst_handle": 601,
         **side("dst")},
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
    return result.returncode, result.stderr, meta_doc, rows


def replay_passes(work: Path, name: str) -> bool:
    result = subprocess.run([sys.executable, str(REPLAY), "--trace-jsonl", str(work / f"{name}.jsonl"),
                             "--trace-meta", str(work / f"{name}.meta.json")], capture_output=True, text=True)
    return result.returncode == 0 and result.stdout.strip() == "PASS"


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
              and meta["uploadBufferUsage"] == "copy-dst-copy-src" and meta["uploadSubmitEvery"] == 1,
              f"{name} backend and upload meta")
        adapter = meta["adapter"]
    rmsnorm = json.loads((args.work / "rmsnorm_tree.meta.json").read_text())
    check(rmsnorm["executionDispatchCount"] == 100 and rmsnorm["executionSubmitCount"] == 2, "rmsnorm dispatch/submit counts")
    atomic = json.loads((args.work / "workgroup_atomic.meta.json").read_text())
    check(atomic["outputOracleCount"] == 1 and atomic["outputOracleMatchedCount"] == 1, "workgroup_atomic oracle did not match")
    check(atomic["outputOracleActualSha256"] == atomic["outputOracleExpectedSha256"], "oracle digests differ")

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
        (["--quirks", str(EXAMPLES / "quirks" / "amd_radv_noop_list.json")], "quirk matching"),
        (["--queue-sync-mode", "deferred"], "per-command"),
        (["--replay", "x"], "not implemented"),
        (["--gpu-timestamp-mode", "require"], "GPU timestamps"),
    ]
    for extra, needle in rejected:
        code, stderr, _, _ = run(args.binary, args.work, "rmsnorm_tree", *extra)
        check(code == 2 and needle in stderr, f"{extra}: exit {code}: {stderr}")
    code, stderr, _, _ = run(args.binary, args.work, "dispatch", commands=EXAMPLES / "dispatch_workgroups_commands.json")
    check(code == 2 and "not implemented" in stderr, f"dispatch stream: exit {code}: {stderr}")

    check_copies(args.binary, args.work)

    print(f"PASS doe-c-runtime: 3 example streams with doe-zig-runtime hash chains, replay-valid artifacts, "
          f"an independent oracle match, alias labels, command repeat, an oracle mismatch, 5 rejections, "
          f"{len(ZIG_COPY_FINAL_HASH)} copy streams, a copy/texture roundtrip read back against host bytes and 4 copy "
          f"failures on "
          f"{adapter.get('device')} ({adapter.get('adapterType')}, {adapter.get('backend')})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
