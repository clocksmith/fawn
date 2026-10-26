#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the C bootstrap oracle against independent hashes and retained Zig."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import tempfile


def run(binary: Path, args: list[str]) -> subprocess.CompletedProcess[bytes]:
    child = subprocess.Popen([str(binary), *args], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = child.communicate(timeout=15)
    except subprocess.TimeoutExpired:
        os.killpg(child.pid, signal.SIGKILL)
        child.communicate()
        raise
    return subprocess.CompletedProcess(child.args, child.returncode, stdout, stderr)


def buffer(elem: str, shape: list[int], values: list[float | int]) -> dict:
    return {"elem": elem, "shape": shape, "values": values}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--c-oracle", type=Path, required=True)
    parser.add_argument("--zig-oracle", type=Path)
    args = parser.parse_args()
    c_binary = args.c_oracle.resolve()
    zig_binary = args.zig_oracle.resolve() if args.zig_oracle else None
    checks = 0
    with tempfile.TemporaryDirectory(prefix="fawn-oracle-") as directory:
        path = Path(directory) / "inputs.json"

        def check(kernel: str, doc: object, *, status: str | None = "pass",
                  expected: bytes | None = None, raw: bytes | None = None,
                  extra: list[str] | None = None) -> None:
            nonlocal checks
            path.write_bytes(raw if raw is not None else json.dumps(doc).encode())
            command = ["--kernel", kernel, "--inputs", str(path), *(extra or [])]
            result = run(c_binary, command)
            if zig_binary:
                reference = run(zig_binary, command)
                assert (result.returncode == 0) == (reference.returncode == 0), (
                    kernel, doc, result, reference)
                assert result.stdout == reference.stdout, (kernel, doc, result, reference)
            if status is None:
                assert result.returncode != 0 and not result.stdout, result
            else:
                assert result.returncode == 0, result
                payload = json.loads(result.stdout)
                assert payload["status"] == status, (doc, payload)
                if expected is not None:
                    assert payload["referenceHash"] == hashlib.sha256(expected).hexdigest(), payload
            checks += 1

        gather = {"kernel": "gather", "inputs": {
            "indices": buffer("u32", [2], [1, 0]),
            "table": buffer("f32", [2, 2], [1, 2, 3, 4])}}
        check("gather", gather, expected=struct.pack("<4f", 3, 4, 1, 2))
        check("doe.tsir.bootstrap.gather", gather,
              expected=struct.pack("<4f", 3, 4, 1, 2),
              extra=["--semantic-tsir", "/missing-unused", "--realization-tsir", "/missing-unused"])
        for elem, code in [("f16", "e"), ("bf16", "H"), ("f32", "f")]:
            doc = {"kernel": "gather", "inputs": {
                "indices": buffer("u32", [1], [0]),
                "table": buffer(elem, [1, 2], [1, -2])}}
            expected = struct.pack("<2" + code, *([0x3f80, 0xc000] if elem == "bf16" else [1, -2]))
            check("gather", doc, expected=expected)
        # Half midpoint + one binary64 step: a binary32 intermediate rounds the wrong way.
        half_value = 1.0004882812500002
        check("gather", {"kernel": "gather", "inputs": {
            "indices": buffer("u32", [1], [0]),
            "table": buffer("f16", [1, 1], [half_value])}}, expected=struct.pack("<e", half_value))
        for value in [65520.0, 1e100, -1e100, 2**-25, 2**-25 + 2**-77, 2**-24, -0.0]:
            check("gather", {"kernel": "gather", "inputs": {
                "indices": buffer("u32", [1], [0]), "table": buffer("f16", [1, 1], [value])}})
        for elem in ["f32", "bf16"]:
            for value in [1e100, 3.4028235e38, 3.4028236e38, -3.4028235e38]:
                check("gather", {"kernel": "gather", "inputs": {
                    "indices": buffer("u32", [1], [0]), "table": buffer(elem, [1, 1], [value])}})
        for kernel in ["fused_gemv", "fused-gemv", "doe.tsir.bootstrap.fused_gemv"]:
            check(kernel, {"kernel": "fused_gemv", "inputs": {
                "W": buffer("f32", [2, 2], [1, 2, 3, 4]),
                "x": buffer("f32", [2], [2, 3])}}, expected=struct.pack("<2f", 8, 18))
        for kernel in ["rms_norm", "rmsnorm", "rms-norm"]:
            check(kernel, {"kernel": "rms_norm", "inputs": {
                "input": buffer("f32", [2], [1, 1]),
                "weight": buffer("f32", [2], [2, 3]),
                "u": {"elem": "u32", "shape": [2], "bytesHex": "0200000000000000"}}},
                  expected=struct.pack("<2f", 2, 3))
        # Array-form inputs and uppercase raw bytes are supported by both bridges.
        check("gather", {"kernel": "gather", "inputs": [
            {"name": "indices", "elem": "u32", "shape": [1], "bytesHex": "00000000"},
            {"name": "table", "elem": "f32", "shape": [1, 1], "bytesHex": "0000803F"}]},
              expected=struct.pack("<f", 1))
        for raw in [b"", b"{", b"[]", b"{}", b'{"kernel":"gather"}',
                    b'{"kernel":0,"inputs":{}}', b'{"kernel":"gather","inputs":0}',
                    b'{"kernel":"gather","kernel":"gather","inputs":{}}']:
            check("gather", None, raw=raw, status="not_implemented")
        for invalid in [buffer("f32", [1.0], [1]), buffer("f32", [-1], []),
                        buffer("f32", [2], [1]), buffer("f32", [1], [True]),
                        buffer("f32", [1], [None]), buffer("u32", [1], [1.0]),
                        buffer("u32", [1], [-1]), buffer("u32", [1], [2**32]),
                        {"elem": "f32", "shape": [1], "bytesHex": "00"}]:
            check("gather", {"kernel": "gather", "inputs": {"table": invalid}}, status="not_implemented")
        check("gather", {"kernel": "other", "inputs": {"bad": {}}}, status="not_implemented")
        check("other", {"kernel": "other", "inputs": {"bad": {}}}, status="not_implemented")
        for doc in [{"kernel": "gather", "inputs": {}}, {"kernel": "other", "inputs": {}},
                    {"kernel": "gather", "inputs": {"table": buffer("i32", [1], [1])}},
                    {"kernel": "gather", "inputs": {"table": buffer("f32\0", [1], [1])}}]:
            check("gather", doc, status="not_implemented")
        check("gather", {"kernel": "gather", "inputs": {
            "indices": buffer("u32", [1], [2]), "table": buffer("f32", [1, 1], [1])}},
              status="not_implemented")
        check("gather", {"kernel": "gather", "inputs": {"table": {
            "elem": "f32", "shape": [1], "bytesHex": "zzzzzzzz"}}}, status=None)
        for shape in [[2**62, 4], [2**62], [0], []]:
            check("gather", {"kernel": "gather", "inputs": {"table": {
                "elem": "f32", "shape": shape, "bytesHex": ""}}}, status="not_implemented")
        # Exact read cap passes; a single excess byte is a process failure with no status.
        valid = json.dumps(gather).encode()
        check("gather", None, raw=valid + b" " * (16 * 1024 * 1024 - len(valid)))
        check("gather", None, raw=valid + b" " * (16 * 1024 * 1024 + 1 - len(valid)), status=None)
        for command in [[], ["--kernel"], ["--help"], ["--kernel", "gather"],
                        ["--inputs", str(path)], ["--kernel", "gather", "--inputs", str(path), "--semantic-tsir"]]:
            result = run(c_binary, command)
            assert result.returncode != 0
            assert json.loads(result.stdout)["detail"] == "invalid arguments"
            assert b"usage:" in result.stderr
            if zig_binary:
                reference = run(zig_binary, command)
                assert reference.returncode != 0 and result.stdout == reference.stdout
            checks += 1
        for target in [Path(directory) / "missing", Path(directory)]:
            command = ["--kernel", "gather", "--inputs", str(target)]
            result = run(c_binary, command)
            assert result.returncode != 0 and not result.stdout
            if zig_binary:
                reference = run(zig_binary, command)
                assert reference.returncode != 0 and not reference.stdout
            checks += 1
    print(f"bootstrap oracle: {checks} checks passed" + (" (C/Zig differential)" if zig_binary else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
