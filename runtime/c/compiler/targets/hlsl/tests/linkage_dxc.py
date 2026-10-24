#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare real DXIL signatures from typed HLSL, without device execution."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def signature(text: str, direction: str) -> dict[int, tuple[str, str, str]]:
    marker = f"; {direction} signature:"
    start = text.index(marker) + len(marker)
    end = text.index(";\n", text.index("; ----", start))
    rows: dict[int, tuple[str, str, str]] = {}
    for line in text[start:end].splitlines():
        fields = line.split()
        if len(fields) < 7 or fields[1] != "TEXCOORD":
            continue
        key = int(fields[2])
        assert key not in rows, f"duplicate location {key}"
        rows[key] = (fields[4], fields[3], fields[6])
    return rows


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=pathlib.Path, help="Built HLSL owner output directory")
    parser.add_argument("dxc", type=pathlib.Path, help="Official DXC executable")
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    out = arguments.output.resolve()
    tool = arguments.dxc.resolve()
    artifacts = out / "linkage"
    artifacts.mkdir(exist_ok=True)
    records: list[dict[str, object]] = []
    inputs: dict[str, str] = {}
    has_completed = False
    result: dict[str, object] = {"scope": "Official DXC host compilation/signature reflection; no device execution",
              "commands": records, "inputs": inputs, "passed": False}

    def run(name: str, command: list[str], expected_failure: bool = False) -> str:
        completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   timeout=20)
        log = artifacts / f"{name}.log"
        log.write_bytes(completed.stdout)
        records.append({"argv": [str(item) for item in command], "exit": completed.returncode,
                        "log_sha256": digest(log), "expected_failure": expected_failure})
        if expected_failure:
            assert completed.returncode != 0, f"{name} unexpectedly accepted"
        else:
            assert completed.returncode == 0, f"{name} failed; see {log}"
        return completed.stdout.decode("utf-8", errors="strict")

    def compile_entry(name: str, entry: str, profile: str, mask: int | None = None,
                      actual_entry: str | None = None) -> str:
        code = artifacts / f"{entry}.hlsl"
        inputs[str(code)] = digest(code)
        output = artifacts / f"{name}.dxil"
        args = [str(tool), "-T", profile, "-E", actual_entry or entry, "-HV", "2021", "-Ges", "-WX", "-Gis",
                "-pack-prefix-stable", "-D", "DOE_HLSL_ENTRY=0"]
        if mask is not None:
            args += ["-D", f"DOE_HLSL_INTERSTAGE_MASK={mask}"]
        args += [str(code), "-Fo", str(output)]
        run(name, args)
        return run(f"{name}-dump", [str(tool), "-dumpbin", str(output)])

    try:
        inputs[str(tool)] = digest(tool)
        producer = out / "test_hlsl"
        inputs[str(producer)] = digest(producer)
        run("producer", [str(producer)] + [str(artifacts / f"old{i}.hlsl") for i in range(10)] +
            [str(artifacts)])
        fragments: dict[str, dict[int, tuple[str, str, str]]] = {}
        expected = {"full": {2, 3, 7, 9, 11, 14, 15}, "subset": {2, 7, 9, 14, 15},
                    "subset_position": {2, 7, 9, 14, 15}, "subset_unused": {2, 7, 9, 14, 15},
                    "no_user": set()}
        for entry, locations in expected.items():
            fragments[entry] = signature(compile_entry(entry, entry, "ps_6_0"), "Input")
            assert fragments[entry].keys() == locations
        checks: list[dict[str, object]] = []
        for vertex in ("vertex", "vertex_reverse"):
            default = signature(compile_entry(f"{vertex}-default", vertex, "vs_6_0"), "Output")
            assert default == fragments["full"]
            # This wrong-but-typed unprojected pair must expose the actual gap:
            # lower unused outputs disturb later shared hardware registers.
            assert any(default[key] != row for key, row in fragments["subset"].items())
            for fragment, rows in fragments.items():
                mask = sum(1 << location for location in rows)
                emitted = signature(compile_entry(f"{vertex}-{fragment}", vertex, "vs_6_0", mask), "Output")
                assert emitted == rows, (vertex, fragment, emitted, rows)
                checks.append({"vertex": vertex, "fragment": fragment, "mask": mask,
                               "signature": emitted})
        high = compile_entry("high-default", "high", "vs_6_0")
        assert signature(high, "Output").keys() == {999}
        rejected = run("high-projected", [str(tool), "-T", "vs_6_0", "-E", "high", "-HV", "2021",
            "-Ges", "-WX", "-Gis", "-pack-prefix-stable", "-D", "DOE_HLSL_INTERSTAGE_MASK=0",
            str(artifacts / "high.hlsl"), "-Fo", str(artifacts / "high-projected.dxil")], True)
        assert "projection location exceeds the 32-bit profile" in rejected
        for source_name, target_name in (("macro_mask", "doe_hlsl_projected_entry"),
                                         ("macro_entry", "doe_hlsl_bundle_entry")):
            mapped = compile_entry(source_name, source_name, "vs_6_0", 0, target_name)
            assert signature(mapped, "Output") == {}
        result["checks"] = checks
        has_completed = True
    finally:
        result["after"] = {path: digest(pathlib.Path(path)) for path in inputs}
        result["unchanged"] = inputs == result["after"]
        result["passed"] = has_completed and result["unchanged"]
        (artifacts / "RESULT.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    assert result["unchanged"]
    print("HLSL linkage:10 exact projected DXIL signature pairs, default/high-location and wrong-pair controls passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
