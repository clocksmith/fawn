#!/usr/bin/env python3
"""Compile and run CPU error/lifetime controls against the pinned built Dawn.

This uses the native Linux Chromium compiler flags and actual Dawn static objects.
It does not start a browser, create a GPU device, or qualify physical execution.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import re
import shutil
import subprocess
import sys

PIN = "758e5e6a974e8ca8039b1d7b209646dabccbcb7f"
OWNERS = ("DeviceWGPU", "BindGroupLayoutWGPU", "PipelineLayoutWGPU",
          "ComputePipelineWGPU", "BindGroupWGPU", "Device")
FIXTURES = ("external-runtime-errors-controls", "external-runtime-callbacks-controls")


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chromium-source", required=True, type=Path)
    parser.add_argument("--build-dir", type=Path, default=Path("out/fawn_release"))
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    if sys.platform != "linux":
        parser.error("these host-executed controls require the native Linux Chromium build")
    source = args.chromium_source.resolve(strict=True)
    dawn = source / "third_party/dawn"
    build = (source / args.build_dir).resolve(strict=True)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    lane = Path(__file__).resolve().parents[1]
    patch = lane / "patches/dawn-doe-runtime.patch"
    result: dict = {"scope": "CPU Dawn error scopes, callback Ref lifetime and DeviceMutex only",
                    "commands": [], "inputs": {}, "passed": False}

    def run(name: str, command: list[str], timeout: int = 240) -> None:
        result["commands"].append({"name": name, "argv": command})
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        with (out / f"{name}.log").open("w") as log:
            subprocess.run(command, cwd=build, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=timeout)

    try:
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=dawn,
                                           text=True).strip()
        result["dawnRevision"] = revision
        if revision != PIN:
            raise RuntimeError(f"unexpected Dawn revision: {revision}")
        run("patch-check", ["git", "-C", str(dawn), "apply", "--reverse", "--check", str(patch)])
        commands: dict[str, list[str]] = {}
        for owner in OWNERS:
            target = f"obj/third_party/dawn/src/dawn/native/sources/{owner}.o"
            raw = subprocess.check_output(["ninja", "-t", "commands", "-s", target],
                                          cwd=build, text=True)
            if len(raw.splitlines()) != 1:
                raise RuntimeError(f"expected one owning compiler command for {target}")
            commands[owner] = shlex.split(raw.strip())
        obj = build / "obj"
        absl = sorted((obj / "third_party/abseil-cpp").rglob("*.o"))
        if not absl:
            raise RuntimeError("built native Abseil source-set objects are absent")
        libraries = [obj / "third_party/dawn/src/dawn/native/libdawn_native_static.a",
                     obj / "third_party/dawn/src/dawn/common/libcommon.a",
                     obj / "third_party/dawn/src/dawn/platform/libdawn_platform_static.a"]
        allocator = obj / "base/allocator/partition_allocator/src/partition_alloc"
        libraries.extend(allocator / name for name in
                         ("liballocator_base.a", "liballocator_core.a",
                          "liballocator_shim.a", "libraw_ptr.a"))
        standard = [obj / "buildtools/third_party/libc++/libc++.a",
                    obj / "buildtools/third_party/libc++abi/libc++abi.a"]
        compiler = (build / commands["DeviceWGPU"][0]).resolve(strict=True)
        ninja = Path(shutil.which("ninja") or "ninja").resolve(strict=True)
        native_objects = [obj / f"third_party/dawn/src/dawn/native/sources/{name}.o"
                          for name in ("Error", "ErrorData", "ErrorScope", "DeviceGuard")]
        native_objects.append(obj / "third_party/dawn/src/utils/utils/log.o")
        selected = [Path(__file__).resolve(), patch, compiler, ninja, *native_objects,
                    *libraries, *absl, *standard,
                    build / "gen/third_party/dawn/include/dawn/webgpu.h"]
        selected.extend(Path(__file__).with_name(f"{name}.cpp") for name in FIXTURES)
        for command in commands.values():
            selected.append((build / command[command.index("-c") + 1]).resolve(strict=True))
        selected.extend(dawn / name for name in re.findall(r"^\+\+\+ b/(.+)$",
                                                           patch.read_text(), re.MULTILINE))
        # Chromium uses thin archives. Bind their actual member bytes too;
        # the archive index alone does not identify the executable code.
        for library in [*libraries, *standard]:
            with library.open("rb") as stream:
                thin = stream.read(8) == b"!<thin>\n"
            if thin:
                for name in subprocess.check_output(["ar", "t", str(library)],
                                                    text=True).splitlines():
                    member = Path(name)
                    selected.append(member if member.is_absolute() else library.parent / member)
        result["inputs"] = {str(path.resolve(strict=True)): digest(path) for path in selected}
        for owner, original in commands.items():
            command = list(original)
            command[command.index("-o") + 1] = str(out / f"{owner}.o")
            command[command.index("-MF") + 1] = str(out / f"{owner}.d")
            run(f"owner-{owner}", command)
        for fixture in FIXTURES:
            command = list(commands["DeviceWGPU"])
            command[command.index("-c") + 1] = str(Path(__file__).with_name(f"{fixture}.cpp"))
            command[command.index("-o") + 1] = str(out / f"{fixture}.o")
            command[command.index("-MF") + 1] = str(out / f"{fixture}.d")
            run(f"{fixture}-compile", command)
            binary = out / fixture
            link = [command[0], "-fuse-ld=lld", "-nostdlib++", "-Wl,--gc-sections",
                    str(out / f"{fixture}.o"), *map(str, native_objects),
                    "-Wl,--start-lib", *map(str, absl),
                    "-Wl,--end-lib", "-Wl,--start-group", *map(str, libraries),
                    *map(str, standard), "-Wl,--end-group",
                    "-ldl", "-lpthread", "-lm", "-lrt", "-latomic", "-o", str(binary)]
            run(f"{fixture}-link", link)
            run(f"{fixture}-run", [str(binary)], timeout=30)
            result.setdefault("binaries", {})[fixture] = digest(binary)
        result["sourceDrift"] = [name for name, value in result["inputs"].items()
                                 if digest(Path(name)) != value]
        result["passed"] = not result["sourceDrift"]
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        result["error"] = str(error)
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed": result["passed"], "out": str(out),
                      "error": result.get("error")}))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
