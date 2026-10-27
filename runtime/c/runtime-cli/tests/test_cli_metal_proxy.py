"""Bounded, explicitly authorized Metal proxy pixels through the actual C CLI."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[4]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path,
                        help="Actual C runtime CLI executable.")
    parser.add_argument("--out", required=True, type=Path,
                        help="Fresh evidence directory.")
    parser.add_argument("--expected", choices=("transparent", "colored"),
                        default="transparent", help="Legacy colored control or repaired Metal proxy.")
    args = parser.parse_args()
    if os.environ.get("DOE_MTL_ALLOW_HARDWARE") != "1":
        parser.error("explicit DOE_MTL_ALLOW_HARDWARE=1 authorization is required")
    args.out.mkdir(parents=True, exist_ok=False)
    commands = args.out / "commands.json"
    pixels = args.out / "pixels.bin"
    metadata = args.out / "trace-meta.json"
    # The sentinel makes a wrong named target or an omitted clear observable.
    document = [
        {"kind": "buffer_write", "handle": 703, "data": [0xA5A5A5A5] * 512},
        {"kind": "copy_buffer_to_texture", "bytes": 2048, "src_handle": 703,
         "src_bytes_per_row": 256, "src_rows_per_image": 8, "dst_handle": 700,
         "dst_width": 8, "dst_height": 8, "dst_format": "rgba8unorm", "dst_usage": 19},
        {"kind": "render_draw", "drawCount": 3, "vertexCount": 3,
         "targetHandle": 700, "targetWidth": 8, "targetHeight": 8,
         "targetFormat": "rgba8unorm", "pipelineMode": "redundant",
         "bindGroupMode": "no-change", "encodeMode": "render-pass"},
        {"kind": "copy_texture_to_buffer", "bytes": 2048, "src_handle": 700,
         "src_width": 8, "src_height": 8, "src_format": "rgba8unorm", "src_usage": 19,
         "dst_handle": 701, "dst_bytes_per_row": 256, "dst_rows_per_image": 8},
    ]
    commands.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    command = [str(args.binary.resolve()), "--commands", str(commands.resolve()),
               "--backend", "native", "--backend-lane", "metal_doe_comparable",
               "--execute", "--quirk-mode", "off", "--kernel-root", "bench/kernels",
               "--gpu-timestamp-mode", "off", "--trace-meta", str(metadata.resolve()),
               "--trace-jsonl", str((args.out / "trace.jsonl").resolve()),
               "--readback-buffer", "701=" + str(pixels.resolve())]
    timed_out = False
    code = None
    with (args.out / "stdout.txt").open("wb") as stdout, (args.out / "stderr.txt").open("wb") as stderr:
        try:
            result = subprocess.run(command, cwd=ROOT, stdout=stdout, stderr=stderr, timeout=20)
            code = result.returncode
        except subprocess.TimeoutExpired:
            timed_out = True
    receipt = {"command": command, "exitCode": code, "timedOut": timed_out,
               "completionUnknown": timed_out,
               "scope": "physical named-target sentinel/clear/render/readback; not cross-runtime equivalence",
               "binarySha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
               "expected": args.expected, "passed": False}
    if code == 0:
        data = pixels.read_bytes()
        info = json.loads(metadata.read_text(encoding="utf-8"))
        receipt["adapterInfo"] = info.get("adapterInfo")
        receipt["shaderSourceReceipts"] = info.get("shaderSourceReceipts")
        expected = bytes((0, 0, 0, 0) if args.expected == "transparent" else (64, 128, 191, 255))
        receipt["pixelSha256"] = hashlib.sha256(data).hexdigest()
        receipt["passed"] = (len(data) == 2048 and
            all(data[y * 256 + x * 4:y * 256 + x * 4 + 4] == expected
                for y in range(8) for x in range(8)) and
            all(data[y * 256 + 32:(y + 1) * 256] == bytes(224) for y in range(8)))
    (args.out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt, indent=2))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
