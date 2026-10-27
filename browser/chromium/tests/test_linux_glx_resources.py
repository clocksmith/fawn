#!/usr/bin/env python3
"""Build/run actual pinned Linux GPU broker controls; never creates a GL context.

Build requires an already-built, managed Chromium tree. Run requires a real X11
session, a DRM render node, and the actual C provider used by the browser.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import resource
import shlex
import stat
import subprocess
import time

OWNER_FILES = (
    "content/common/gpu_pre_sandbox_hook_linux.cc",
    "content/common/gpu_pre_sandbox_hook_linux.h",
    "content/common/BUILD.gn",
    "content/gpu/gpu_main.cc",
    "ui/ozone/public/ozone_platform.cc",
    "ui/ozone/public/ozone_platform.h",
)
TARGETS = {
    "gpu_pre_sandbox_hook_linux": "obj/content/common/common/gpu_pre_sandbox_hook_linux.o",
    "gpu_main": "obj/content/gpu/gpu_sources/gpu_main.o",
    "ozone_platform": "obj/ui/ozone/platform/ozone_platform.o",
}
SOURCE_SET_OWNERS = (
    "obj/third_party/abseil-cpp/", "obj/third_party/perfetto/",
    "obj/third_party/boringssl/", "obj/third_party/zlib/",
    "obj/gpu/config/", "obj/ui/gl/", "obj/ui/ozone/ozone_switches/",
)
PLATFORM_OBJECTS = {
    "obj/ui/ozone/platform/platform_selection.o",
    "obj/ui/ozone/platform/platform_list.o",
}


def sha256(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n")


def limit_child() -> None:
    resource.setrlimit(resource.RLIMIT_AS, (20 * 1024**3, 20 * 1024**3))


def execute(command: list[str], cwd: Path, out: Path, name: str) -> None:
    write_json(out / (name + "-command.json"), command)
    with (out / (name + ".log")).open("wb") as log:
        subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT,
                       timeout=240, check=True, preexec_fn=limit_child)


def build(args: argparse.Namespace) -> None:
    root = args.chromium_root.resolve()
    build_dir = root / args.build_dir
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    control = Path(__file__).with_name("linux_glx_resource_controls.cpp").resolve()
    files = [root / name for name in OWNER_FILES] + [control, Path(__file__).resolve()]
    inputs = {str(path): sha256(path) for path in files}
    write_json(out / "source-inputs-before.json", inputs)
    try:
        compile_commands: dict[str, list[str]] = {}
        for name, target in TARGETS.items():
            raw = subprocess.check_output([args.ninja, "-t", "commands", "-s", target],
                                          cwd=build_dir, text=True)
            if len(raw.splitlines()) != 1:
                raise RuntimeError("Expected exactly one pinned owning TU command: " + target)
            command = shlex.split(raw.strip())
            if not command[0].endswith("/clang++"):
                raise RuntimeError("Unexpected pinned compiler: " + command[0])
            command[command.index("-o") + 1] = str(out / (name + ".o"))
            command[command.index("-MF") + 1] = str(out / (name + ".d"))
            execute(command, build_dir, out, name)
            compile_commands[name] = command
        command = compile_commands["gpu_pre_sandbox_hook_linux"].copy()
        command[command.index("-c") + 1] = str(control)
        command[command.index("-o") + 1] = str(out / "controls.o")
        command[command.index("-MF") + 1] = str(out / "controls.d")
        execute(command, build_dir, out, "controls-compile")
        graph = subprocess.check_output([args.ninja, "-t", "query", "chrome"],
                                        cwd=build_dir, text=True)
        (out / "actual-chrome-inputs.txt").write_text(graph)
        direct = [line.strip() for line in graph.splitlines()
                  if line.startswith("    ") and line.strip().endswith(".o")]
        objects = [build_dir / name for name in direct
                   if name.startswith(SOURCE_SET_OWNERS) or name in PLATFORM_OBJECTS]
        archives = sorted((build_dir / "obj").rglob("*.a"))
        archives += sorted((build_dir / "obj").rglob("*.rlib"))
        dependencies = objects + archives
        ar = (build_dir / compile_commands["gpu_main"][0]).resolve().with_name("llvm-ar")
        for archive in archives:
            with archive.open("rb") as stream:
                thin = stream.read(8) == b"!<thin>\n"
            if thin:
                members = subprocess.check_output([str(ar), "t", str(archive)], text=True).splitlines()
                for name in members:
                    member = Path(name)
                    if not member.is_absolute():
                        member = archive.parent / member
                    if not member.is_file():
                        raise RuntimeError("Missing actual thin-archive member: " + str(member))
                    dependencies.append(member)
        ninja_path = shutil.which(args.ninja)
        if not ninja_path:
            raise RuntimeError("Pinned Ninja is unavailable")
        dependencies += [ar, Path(ninja_path), (build_dir / compile_commands["gpu_main"][0]).resolve()]
        link_inputs = {str(path): sha256(path) for path in dependencies}
        write_json(out / "link-inputs-before.json", link_inputs)
        clang = (build_dir / compile_commands["gpu_main"][0]).resolve()
        sysroot = root / "build/linux/debian_bullseye_amd64-sysroot"
        command = [str(clang), "-fuse-ld=lld", "-Wl,--gc-sections", "-Wl,--no-undefined",
                   "-nostdlib++", "--sysroot=" + str(sysroot), "-o", str(out / "controls"),
                   str(out / "controls.o"), str(out / "ozone_platform.o"),
                   *map(str, objects), "-Wl,--start-group", *map(str, archives),
                   "-Wl,--end-group", "-ldl", "-lpthread", "-lm", "-lrt", "-lX11", "-lXext", "-lxcb"]
        execute(command, build_dir, out, "controls-link")
        drift = [path for path, digest in link_inputs.items() if sha256(Path(path)) != digest]
        if drift:
            raise RuntimeError("Pinned link inputs changed: " + repr(drift))
        write_json(out / "build-result.json", {
            "scope": "Actual owning TUs and CPU broker control; no browser/GPU acceptance",
            "binarySha256": sha256(out / "controls"), "returnCode": 0,
        })
    finally:
        drift = [path for path, digest in inputs.items() if sha256(Path(path)) != digest]
        write_json(out / "source-verdict.json", {"sourceDrift": drift})
        if drift:
            raise RuntimeError("Pinned source changed: " + repr(drift))


def run(args: argparse.Namespace) -> None:
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve()
    node = args.drm_node.absolute()
    info = node.lstat()
    if not stat.S_ISCHR(info.st_mode) or os.major(info.st_rdev) != 226:
        raise RuntimeError("Expected an actual DRM character node")
    device = Path(f"/sys/dev/char/{os.major(info.st_rdev)}:{os.minor(info.st_rdev)}/device").resolve(strict=True)
    vendor = int((device / "vendor").read_text().strip(), 16)
    device_id = int((device / "device").read_text().strip(), 16)
    # The independent accelerated metadata oracle is deliberately Mesa-only.
    if vendor not in (0x8086, 0x1002, 0x1AF4):
        raise RuntimeError("This CPU metadata fixture requires an actual Mesa GPU")
    configs = [path for path in sorted(Path("/usr/share/drirc.d").glob("*.conf"))
               if stat.S_ISREG(path.lstat().st_mode) and path.stat().st_uid == 0
               and not path.stat().st_mode & 0o022]
    if not configs:
        raise RuntimeError("No trusted system drirc fixture exists")
    common = [f"--expected-vendor={vendor:x}", f"--expected-device={device_id:x}",
              "--drm-node=" + str(node), "--drirc-file=" + str(configs[0])]
    inputs = {str(binary): sha256(binary), str(args.provider.resolve()): sha256(args.provider.resolve())}
    write_json(out / "inputs.json", {"files": inputs, "device": str(device),
               "vendor": vendor, "deviceId": device_id, "rdev": info.st_rdev})
    cases = [
        ("external-x11", ["--use-webgpu-runtime=doe", "--ozone-platform=x11", "--expect-glx-resources"]),
        ("native-dawn", ["--use-webgpu-runtime=dawn", "--ozone-platform=x11"]),
        ("wayland", ["--use-webgpu-runtime=doe", "--ozone-platform=wayland"]),
        ("vulkan", ["--use-webgpu-runtime=doe", "--ozone-platform=x11", "--use-angle=vulkan"]),
        ("software", ["--use-webgpu-runtime=doe", "--ozone-platform=x11", "--use-angle=swiftshader"]),
        ("disabled", ["--use-webgpu-runtime=doe", "--ozone-platform=x11", "--disable-webgpu-doe"]),
        ("late", ["--use-webgpu-runtime=doe", "--ozone-platform=x11", "--late-policy"]),
        ("metadata-baseline", ["--type=gpu-process", "--use-webgpu-runtime=doe", "--ozone-platform=x11", "--metadata-baseline"]),
        ("metadata-sandbox", ["--type=gpu-process", "--use-webgpu-runtime=doe", "--ozone-platform=x11", "--metadata-sandbox",
                              "--doe-webgpu-library-path=" + str(args.provider.resolve())]),
    ]
    rows = []
    baseline = None
    try:
        for name, flags in cases:
            if name == "metadata-sandbox":
                flags.append("--expected-configs=" + baseline[2])
            command = [str(binary), *common, *flags]
            start = time.monotonic()
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       start_new_session=True)
            timed_out = False
            try:
                stdout, stderr = process.communicate(timeout=30)
            except subprocess.TimeoutExpired:
                timed_out = True
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    stdout, stderr = process.communicate(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    stdout, stderr = process.communicate(timeout=3)
            (out / (name + ".stdout")).write_bytes(stdout)
            (out / (name + ".stderr")).write_bytes(stderr)
            rows.append({"id": name, "argv": command, "returnCode": process.returncode,
                         "timedOut": timed_out,
                         "elapsedSeconds": time.monotonic() - start})
            write_json(out / "results.json", {"scope": "Actual CPU broker and hardware metadata only; no GL context/GPU work", "cases": rows})
            if process.returncode or timed_out:
                raise RuntimeError("CPU control failed; inspect retained stdout/stderr: " + name)
            if name.startswith("metadata-"):
                match = re.search(rb"GLX_METADATA version=(\d+)\.(\d+) configs=(\d+) extensions=(\d+) vendor=([0-9a-f]+) device=([0-9a-f]+) accelerated=(\d+)", stdout)
                if not match:
                    raise RuntimeError("Missing actual metadata observation")
                observed = tuple(value.decode() for value in match.groups())
                if name == "metadata-baseline":
                    baseline = observed
                elif observed != baseline or b"PRE_SEAL fixed library/resource preparation threads=1" not in stdout:
                    raise RuntimeError("Protected metadata differs from independent baseline")
        write_json(out / "verdict.json", {"passed": True, "baseline": baseline, "nativePixelClaim": False, "browserClaim": False})
    finally:
        drift = [path for path, digest in inputs.items() if sha256(Path(path)) != digest]
        write_json(out / "source-verdict.json", {"sourceDrift": drift})
        if drift:
            raise RuntimeError("CPU inputs changed: " + repr(drift))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="operation", required=True)
    build_parser = sub.add_parser("build")
    build_parser.add_argument("--chromium-root", type=Path, required=True)
    build_parser.add_argument("--build-dir", default="out/fawn_release")
    build_parser.add_argument("--ninja", default="ninja")
    build_parser.add_argument("--out", type=Path, required=True)
    run_parser = sub.add_parser("run")
    run_parser.add_argument("--binary", type=Path, required=True)
    run_parser.add_argument("--provider", type=Path, required=True)
    run_parser.add_argument("--drm-node", type=Path, default=Path("/dev/dri/renderD128"))
    run_parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    (build if args.operation == "build" else run)(args)


if __name__ == "__main__":
    main()
