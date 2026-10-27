#!/usr/bin/env python3
"""Compile the managed Dawn projection against its real generated ABI header.

This is a CPU boundary control, not a browser/GPU qualification command.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dawn-source", required=True, type=Path)
    parser.add_argument("--webgpu-header", required=True, type=Path)
    parser.add_argument("--doe-library", required=True, type=Path)
    parser.add_argument("--expected-private-policy", required=True, choices=["present", "absent"])
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    lane = Path(__file__).resolve().parents[1]
    patch = lane / "patches/dawn-doe-runtime.patch"
    fixture = Path(__file__).with_name("external-runtime-projection-controls.cpp")
    source = args.dawn_source.resolve(strict=True)
    header = args.webgpu_header.resolve(strict=True)
    library = args.doe_library.resolve(strict=True)
    compiler = Path(shutil.which(args.cxx) or args.cxx).resolve(strict=True)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    helper = source / "src/dawn/native/webgpu/InnerRuntimeDescriptor.h"
    selected = [patch, fixture, helper, header, library, compiler]
    selected.extend(sorted(library.parent.glob("libdoe_compiler.*")))
    inputs = {str(path): digest(path) for path in selected}
    result = {"scope": "CPU external/native descriptor boundary only; no adapter/device/browser creation",
              "inputs": inputs, "expectedPrivatePolicy": args.expected_private_policy,
              "loaderEnvironment": {name: os.environ[name] for name in ["LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"] if name in os.environ},
              "passed": False, "commands": []}
    (out / "inputs.json").write_text(json.dumps(result, indent=2) + "\n")
    try:
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
        result["dawnRevision"] = revision
        if revision != "758e5e6a974e8ca8039b1d7b209646dabccbcb7f":
            raise RuntimeError(f"unexpected Dawn revision: {revision}")
        with (out / "patch-check.log").open("w") as log:
            subprocess.run(["git", "apply", "--reverse", "--check", str(patch)], cwd=source,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        include = out / "include/dawn"
        include.mkdir(parents=True)
        shutil.copy2(header, include / "webgpu.h")
        binary = out / ("control.exe" if sys.platform == "win32" else "control")
        command = [str(compiler), "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   "-I", str(out / "include"), "-I", str(source), str(fixture), "-o", str(binary)]
        if sys.platform.startswith("linux"):
            command.append("-ldl")
        result["commands"].append(command)
        with (out / "build.log").open("w") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
        result["binarySha256"] = digest(binary)
        command = [str(binary), str(library), args.expected_private_policy]
        result["commands"].append(command)
        with (out / "control.log").open("w") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=20)
        result["sourceDrift"] = [name for name, value in inputs.items() if digest(Path(name)) != value]
        result["passed"] = not result["sourceDrift"]
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        result["error"] = str(error)
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed": result["passed"], "out": str(out), "error": result.get("error")}))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
