#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run one explicitly authorized bounded Metal correctness graph, never timing."""
from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def durable(path: Path, value: object) -> None:
    with path.open("x", encoding="utf-8") as output:
        json.dump(value, output, indent=2)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="store_true", required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--source-inputs", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != "Darwin":
        parser.error("requires an explicitly authorized physical macOS device")
    root = Path(__file__).resolve().parents[4]
    source = json.loads(args.source_inputs.read_text())
    expected = source["files"]

    def drift() -> list[str]:
        failures = []
        for name, wanted in expected.items():
            relative = Path(name)
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError("source input path escapes the frozen root")
            path = root / relative
            actual = "symlink:" + os.readlink(path) if path.is_symlink() else digest(path) if path.is_file() else None
            if actual != wanted:
                failures.append(name)
        return failures

    if drift():
        raise ValueError("frozen source changed before physical admission")
    args.out = args.out.resolve()
    args.out.mkdir(exist_ok=False, parents=True)
    images = {name: {"path": str(path.resolve(strict=True)), "sha256": digest(path)}
              for name, path in (("binary", args.binary), ("native", args.native), ("compiler", args.compiler))}
    os_build = subprocess.check_output(["sw_vers"], text=True, timeout=5)
    hardware = subprocess.check_output(["sysctl", "-n", "hw.model"], text=True, timeout=5).strip()
    identity = {"source": source, "sourceInputSha256": digest(args.source_inputs), "images": images,
                "os": platform.uname()._asdict(), "osBuild": os_build, "hardwareModel": hardware,
                "scope": "one native physical Metal correctness graph; no performance or browser qualification",
                "completionDeadlineSeconds": 5, "processDeadlineSeconds": 20}
    durable(args.out / "inputs.json", identity)
    env = os.environ.copy()
    for key in ("DOE_WEBGPU_LIB", "DOE_LIB", "DOE_C_STAGE", "DYLD_INSERT_LIBRARIES",
                "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
        env.pop(key, None)
    env.update(DOE_MTL_ALLOW_HARDWARE="1", DOE_C_TEST_BACKEND="metal", DOE_C_TEST_ADAPTER="physical",
               DOE_SHADER_CACHE_DIR=str(args.out / "shader-cache"))
    command = [images["binary"]["path"], "--run", str(args.out), images["native"]["path"], images["compiler"]["path"]]
    durable(args.out / "dispatch.json", {"command": command, "gpuCompletion": "not-dispatched"})
    timed_out = False
    started = time.monotonic()
    with (args.out / "stdout.log").open("xb") as stdout, (args.out / "stderr.log").open("xb") as stderr:
        process = subprocess.Popen(command, cwd=root / "runtime/c/native", env=env, stdout=stdout, stderr=stderr)
        try:
            code = process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            code = process.wait()
        stdout.flush()
        stderr.flush()
        os.fsync(stdout.fileno())
        os.fsync(stderr.fileno())
    elapsed = time.monotonic() - started
    events = []
    journal = args.out / "checkpoints.jsonl"
    if journal.is_file():
        events = [json.loads(line) for line in journal.read_text().splitlines()]
    failures = []
    if (timed_out or code != 0 or not events or events[-1].get("phase") != "terminal"
            or events[-1].get("gpuCompletion") != "completed" or events[-1].get("errors") != 0):
        failures.append("native process did not prove terminal success")
    captures = []
    if not failures:
        for arm in range(3):
            values = array("I", ((i * 3 + 10) if arm == 0 else ((i % 1021) * 3 + 22)
                                 if arm == 1 else (i * 9 + 37) for i in range(262144)))
            if sys.byteorder != "little":
                values.byteswap()
            wanted = hashlib.sha256(values.tobytes()).hexdigest()
            path = args.out / f"capture-{arm}.bin"
            actual = digest(path) if path.is_file() else None
            size = path.stat().st_size if path.is_file() else 0
            captures.append({"arm": arm, "bytes": size, "sha256": actual, "expectedSha256": wanted})
            if size != 1048576 or actual != wanted:
                failures.append(f"full nonzero independent readback oracle failed for arm {arm}")
    source_drift = drift()
    if source_drift:
        failures.append("frozen source changed during physical control")
    for name, image in images.items():
        if digest(Path(image["path"])) != image["sha256"]:
            failures.append(f"loaded image changed: {name}")
    verdict = {"scope": identity["scope"], "exitCode": code, "timedOut": timed_out,
               "processElapsedSeconds": elapsed, "sourceDrift": source_drift,
               "captures": captures, "failures": failures,
               "gpuCompletion": events[-1].get("gpuCompletion", "unknown") if events else "unknown",
               "status": "pass" if not failures else "failed"}
    durable(args.out / "verdict.json", verdict)
    print(json.dumps(verdict))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
