#!/usr/bin/env python3
"""Run the production Skia fallback byte-layout controls on native Linux.

Compile the decoder for Linux and optionally an existing macOS cross-build;
execute only actual CPU raster read/write operations, never a browser or GPU.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys

PIN = "4a6fb38567da21dc86f8ba468ac0ca9002f4f606"
TARGET = "obj/gpu/command_buffer/service/gles2_sources/webgpu_decoder_impl.o"
OBJECT_PREFIXES = ("obj/skia/", "obj/third_party/skcms/", "obj/third_party/abseil-cpp/",
                   "obj/third_party/perfetto/", "obj/third_party/boringssl/",
                   "obj/third_party/zlib/")
VIZ_OBJECTS = (
    "obj/components/viz/common/resources/shared_image_format/shared_image_format.o",
    "obj/components/viz/common/resources/shared_image_format/shared_image_format_utils.o",
)


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def compiler_command(build: Path) -> list[str]:
    raw = subprocess.check_output(["ninja", "-t", "commands", "-s", TARGET],
                                  cwd=build, text=True)
    if len(raw.splitlines()) != 1:
        raise RuntimeError("expected exactly one decoder compiler command")
    return shlex.split(raw.strip())


def graph_inputs(build: Path) -> tuple[list[Path], list[Path], str]:
    raw = subprocess.check_output(["ninja", "-t", "query", "chrome"], cwd=build, text=True)
    objects, libraries = [], []
    for line in raw.splitlines()[1:]:
        name = line.strip().removeprefix("| ").removeprefix("|| ")
        if name == "outputs:":
            break
        if name.endswith(".o") and (name.startswith(OBJECT_PREFIXES) or name in VIZ_OBJECTS):
            objects.append((build / name).resolve(strict=True))
        elif name.endswith((".a", ".rlib")):
            libraries.append((build / name).resolve(strict=True))
    if not objects or not libraries:
        raise RuntimeError("native static Chrome direct-link graph is absent")
    # GN owns the viz implementation in a static archive, rather than as a
    # direct source-set object. Verify that exact graph owner and its members.
    viz_library = (build / "obj/components/viz/common/resources/libviz_shared_image_format.a").resolve(strict=True)
    if viz_library not in libraries:
        raise RuntimeError("actual viz format archive is not on Chrome's link edge")
    members = subprocess.check_output(["ar", "t", str(viz_library)], text=True).splitlines()
    for name in VIZ_OBJECTS:
        if not any(Path(member).name == Path(name).name for member in members):
            raise RuntimeError(f"actual viz format implementation is missing from its archive: {name}")
    return list(dict.fromkeys(objects)), list(dict.fromkeys(libraries)), raw


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chromium-source", required=True, type=Path)
    parser.add_argument("--build-dir", default=Path("out/fawn_release"), type=Path)
    parser.add_argument("--mac-build-dir", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    if sys.platform != "linux":
        parser.error("the CPU executable requires the native Linux Chromium build")
    source = args.chromium_source.resolve(strict=True)
    build = (source / args.build_dir).resolve(strict=True)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    lane = Path(__file__).resolve().parents[1]
    patch = lane / "patches/chromium-doe-runtime.patch"
    fixture = Path(__file__).with_name("skia-fallback-pixels-controls.cpp")
    helper = source / "gpu/command_buffer/service/webgpu_skia_fallback.h"
    decoder = source / "gpu/command_buffer/service/webgpu_decoder_impl.cc"
    result: dict = {"scope": "real Skia raster CPU pixels and decoder translation units only",
                    "commands": [], "inputs": {}, "passed": False}

    def run(name: str, command: list[str], cwd: Path, timeout: int = 300) -> None:
        result["commands"].append({"name": name, "argv": command, "cwd": str(cwd)})
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        with (out / f"{name}.log").open("w") as log:
            subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=timeout)

    try:
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source,
                                           text=True).strip()
        result["chromiumRevision"] = revision
        if revision != PIN:
            raise RuntimeError(f"unexpected Chromium revision: {revision}")
        run("patch-check", ["git", "apply", "--reverse", "--check", str(patch)], source)
        original = compiler_command(build)
        compiler = (build / original[0]).resolve(strict=True)
        objects, libraries, query = graph_inputs(build)
        (out / "chrome-direct-inputs.txt").write_text(query)
        selected = [Path(__file__).resolve(), patch, fixture, helper, decoder, compiler,
                    source / "components/viz/common/resources/shared_image_format_utils.cc",
                    *objects, *libraries]
        # Thin archive indexes do not bind their implementation bytes.
        for library in libraries:
            with library.open("rb") as stream:
                is_thin = stream.read(8) == b"!<thin>\n"
            if is_thin:
                for name in subprocess.check_output(["ar", "t", str(library)],
                                                    text=True).splitlines():
                    member = Path(name)
                    selected.append(member if member.is_absolute() else library.parent / member)
        result["inputs"] = {str(path.resolve(strict=True)): digest(path) for path in selected}
        targets = [("linux", build, original)]
        if args.mac_build_dir:
            mac = (source / args.mac_build_dir).resolve(strict=True)
            targets.append(("macos", mac, compiler_command(mac)))
        for name, target_build, original_command in targets:
            command = list(original_command)
            command[command.index("-o") + 1] = str(out / f"decoder-{name}.o")
            command[command.index("-MF") + 1] = str(out / f"decoder-{name}.d")
            run(f"decoder-{name}", command, target_build)
        command = list(original)
        command[command.index("-c") + 1] = str(fixture)
        command[command.index("-o") + 1] = str(out / "controls.o")
        command[command.index("-MF") + 1] = str(out / "controls.d")
        run("controls-compile", command, build)
        # Preserve the build's actual sysroot; implementation objects come from
        # Chrome's direct link edge, rather than replacement Skia implementations.
        sysroot = [arg for arg in original if arg.startswith("--sysroot=")]
        link = [str(compiler), "-fuse-ld=lld", "-Wl,--gc-sections", "-Wl,--no-undefined",
                "-nostdlib++", *sysroot, str(out / "controls.o"), "-Wl,--start-lib",
                *map(str, objects), "-Wl,--end-lib", "-Wl,--start-group",
                *map(str, libraries), "-Wl,--end-group", "-ldl", "-lpthread", "-lm",
                "-lrt", "-lX11", "-lXext", "-lxcb", "-o", str(out / "controls")]
        run("controls-link", link, build)
        run("controls-run", [str(out / "controls")], build, timeout=30)
        result["binarySha256"] = digest(out / "controls")
        result["sourceDrift"] = [name for name, value in result["inputs"].items()
                                 if digest(Path(name)) != value]
        result["passed"] = not result["sourceDrift"]
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        result["error"] = str(error)
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed": result["passed"], "out": str(out), "error": result.get("error")}))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
