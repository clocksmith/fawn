#!/usr/bin/env python3
"""Check the real default-selector header and pinned Linux/Mac decoder TUs."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess

import test_linux_glx_resources as owner

OWNER_FILES = (
    "gpu/command_buffer/service/webgpu_linux_backend_policy.h",
    "gpu/command_buffer/service/webgpu_decoder_impl.cc",
    "gpu/command_buffer/service/BUILD.gn",
    "gpu/command_buffer/service/webgpu_skia_fallback.h",
)
TARGET = "obj/gpu/command_buffer/service/gles2_sources/webgpu_decoder_impl.o"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chromium-source", type=Path, required=True)
    parser.add_argument("--linux-build", default="out/fawn_release")
    parser.add_argument("--mac-build", default="out/fawn_macos_arm64")
    parser.add_argument("--ninja", default="ninja")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    source = args.chromium_source.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    fixture = Path(__file__).with_name("linux_webgpu_backend_policy_controls.cpp").resolve()
    paths = [source / name for name in OWNER_FILES]
    paths += [fixture, Path(__file__).resolve(), Path(owner.__file__).resolve()]
    builds = {"linux": source / args.linux_build, "macos": source / args.mac_build}
    paths += [path / "args.gn" for path in builds.values()]
    inputs = {str(path): owner.sha256(path) for path in paths}
    owner.write_json(out / "source-inputs-before.json", inputs)
    ninja = shutil.which(args.ninja)
    if not ninja:
        raise RuntimeError("Pinned Ninja is unavailable")
    tools = {str(Path(ninja).resolve()): owner.sha256(Path(ninja))}
    result = {"scope": "Default policy CPU controls plus pinned Linux and arm64-Mac decoder compilation; no browser/GPU admission",
              "checks": []}
    try:
        commands = {}
        for platform, build in builds.items():
            raw = subprocess.check_output([args.ninja, "-t", "commands", "-s", TARGET],
                                          cwd=build, text=True)
            if len(raw.splitlines()) != 1:
                raise RuntimeError("Expected one actual decoder TU command: " + platform)
            command = shlex.split(raw.strip())
            if not command[0].endswith("/clang++"):
                raise RuntimeError("Unexpected pinned compiler: " + command[0])
            compiler = (build / command[0]).resolve()
            tools[str(compiler)] = owner.sha256(compiler)
            owner.write_json(out / "toolchain-inputs-before.json", tools)
            actual_source = build / command[command.index("-c") + 1]
            if owner.sha256(actual_source) != inputs[str(source / OWNER_FILES[1])]:
                raise RuntimeError("Owning command does not compile the selected source")
            command[command.index("-o") + 1] = str(out / (platform + "-decoder.o"))
            command[command.index("-MF") + 1] = str(out / (platform + "-decoder.d"))
            owner.execute(command, build, out, platform + "-decoder")
            commands[platform] = command
            result["checks"].append({"id": platform + "-decoder", "returnCode": 0})
            owner.write_json(out / "results.json", result)
        linux = builds["linux"]
        command = commands["linux"].copy()
        command[command.index("-c") + 1] = str(fixture)
        command[command.index("-o") + 1] = str(out / "controls.o")
        command[command.index("-MF") + 1] = str(out / "controls.d")
        owner.execute(command, linux, out, "controls-compile")
        clang = (linux / command[0]).resolve()
        link = [str(clang), "-fuse-ld=lld", "-nostdlib++",
                "--sysroot=" + str(source / "build/linux/debian_bullseye_amd64-sysroot"),
                str(out / "controls.o"), "-o", str(out / "controls")]
        owner.execute(link, linux, out, "controls-link")
        owner.execute([str(out / "controls")], out, out, "controls-run")
        text = (out / "controls-run.log").read_text()
        if "PASS 16 default backend policy cases" not in text:
            raise RuntimeError("Missing complete always-on policy control witness")
        result["checks"].append({"id": "default-policy-cpu", "returnCode": 0,
                                 "cases": 16, "binarySha256": owner.sha256(out / "controls")})
        result["passed"] = True
        owner.write_json(out / "results.json", result)
    finally:
        drift = [path for path, digest in inputs.items() if owner.sha256(Path(path)) != digest]
        tool_drift = [path for path, digest in tools.items() if owner.sha256(Path(path)) != digest]
        owner.write_json(out / "source-verdict.json", {"sourceDrift": drift, "toolchainDrift": tool_drift})
        if drift or tool_drift:
            raise RuntimeError("Selected inputs changed: " + json.dumps(drift + tool_drift))


if __name__ == "__main__":
    main()
