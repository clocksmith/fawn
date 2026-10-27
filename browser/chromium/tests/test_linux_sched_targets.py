#!/usr/bin/env python3
"""Compile actual GPU policy owners and exercise their seccomp filter; no GPU."""
from __future__ import annotations

import argparse
import ctypes
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import time

import test_linux_glx_resources as owner

TARGETS = {
    "sigsys_handlers": "obj/sandbox/linux/seccomp_bpf/sigsys_handlers.o",
    "syscall_parameters_restrictions": "obj/sandbox/linux/seccomp_bpf/syscall_parameters_restrictions.o",
    "sandbox_linux": "obj/sandbox/policy/policy/sandbox_linux.o",
    "sandbox_seccomp_bpf_linux": "obj/sandbox/policy/policy/sandbox_seccomp_bpf_linux.o",
    **owner.TARGETS,
}
for _policy in ("gpu", "cros_amd_gpu", "cros_arm_gpu", "cros_intel_gpu",
                "cros_nvidia_gpu", "cros_virtio_gpu"):
    _name = "bpf_" + _policy + "_policy_linux"
    TARGETS[_name] = "obj/sandbox/policy/policy/" + _name + ".o"

HEADERS = (
    "sandbox/linux/seccomp-bpf-helpers/sigsys_handlers.h",
    "sandbox/linux/seccomp-bpf-helpers/syscall_parameters_restrictions.h",
    "sandbox/policy/linux/bpf_gpu_policy_linux.h",
    "sandbox/policy/linux/sandbox_seccomp_bpf_linux.h",
    "sandbox/policy/linux/sandbox_linux.h",
)


def build(args: argparse.Namespace) -> None:
    root = args.chromium_root.resolve()
    cwd = root / args.build_dir
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    fixture = Path(__file__).with_name("linux_sched_target_controls.cpp").resolve()
    files = [root / name for name in (*HEADERS, *owner.OWNER_FILES)]
    files += [fixture, Path(__file__).resolve(), Path(owner.__file__).resolve(), cwd / "args.gn"]
    commands = {}
    for name, target in TARGETS.items():
        raw = subprocess.check_output([args.ninja, "-t", "commands", "-s", target],
                                      cwd=cwd, text=True)
        if len(raw.splitlines()) != 1:
            raise RuntimeError("Expected one pinned owning-TU command: " + target)
        command = shlex.split(raw.strip())
        if not command[0].endswith("/clang++"):
            raise RuntimeError("Unexpected compiler: " + command[0])
        files.append((cwd / command[command.index("-c") + 1]).resolve())
        command[command.index("-o") + 1] = str(out / (name + ".o"))
        command[command.index("-MF") + 1] = str(out / (name + ".d"))
        commands[name] = command
    inputs = {str(path): owner.sha256(path) for path in files}
    owner.write_json(out / "source-inputs-before.json", inputs)
    try:
        for name, command in commands.items():
            owner.execute(command, cwd, out, name)
        command = commands["gpu_pre_sandbox_hook_linux"].copy()
        command[command.index("-c") + 1] = str(fixture)
        command[command.index("-o") + 1] = str(out / "controls.o")
        command[command.index("-MF") + 1] = str(out / "controls.d")
        owner.execute(command, cwd, out, "controls-compile")
        graph = subprocess.check_output([args.ninja, "-t", "query", "chrome"],
                                       cwd=cwd, text=True)
        (out / "actual-chrome-inputs.txt").write_text(graph)
        direct = [line.strip() for line in graph.splitlines()
                  if line.startswith("    ") and line.strip().endswith(".o")]
        objects = [cwd / name for name in direct
                   if name.startswith(owner.SOURCE_SET_OWNERS) or name in owner.PLATFORM_OBJECTS]
        archives = sorted((cwd / "obj").rglob("*.a"))
        archives += sorted((cwd / "obj").rglob("*.rlib"))
        clang = (cwd / command[0]).resolve()
        ar = clang.with_name("llvm-ar")
        ninja = shutil.which(args.ninja)
        if not ninja:
            raise RuntimeError("Pinned Ninja is unavailable")
        dependencies = [*objects, *archives, ar, clang, Path(ninja)]
        for archive in archives:
            with archive.open("rb") as stream:
                thin = stream.read(8) == b"!<thin>\n"
            if thin:
                for name in subprocess.check_output([str(ar), "t", str(archive)], text=True).splitlines():
                    member = Path(name)
                    if not member.is_absolute():
                        member = archive.parent / member
                    dependencies.append(member)
        link_inputs = {str(path): owner.sha256(path) for path in dependencies}
        owner.write_json(out / "link-inputs-before.json", link_inputs)
        fresh = [out / (name + ".o") for name in commands if name != "gpu_main"]
        command = [str(clang), "-fuse-ld=lld", "-Wl,--gc-sections", "-Wl,--no-undefined",
                   "-nostdlib++", "--sysroot=" + str(root / "build/linux/debian_bullseye_amd64-sysroot"),
                   "-o", str(out / "controls"), str(out / "controls.o"), *map(str, fresh),
                   *map(str, objects), "-Wl,--start-group", *map(str, archives),
                   "-Wl,--end-group", "-ldl", "-lpthread", "-lm", "-lrt", "-lX11", "-lXext", "-lxcb"]
        owner.execute(command, cwd, out, "controls-link")
        drift = [path for path, digest in link_inputs.items() if owner.sha256(Path(path)) != digest]
        if drift:
            raise RuntimeError("Actual link inputs changed: " + repr(drift))
        owner.write_json(out / "build-result.json", {
            "passed": True, "owningTranslationUnits": len(commands),
            "binarySha256": owner.sha256(out / "controls"),
            "scope": "Actual GPU policy and BPF CPU control; no browser or GPU qualification",
        })
    finally:
        drift = [path for path, digest in inputs.items() if owner.sha256(Path(path)) != digest]
        owner.write_json(out / "source-verdict.json", {"sourceDrift": drift})
        if drift:
            raise RuntimeError("Source inputs changed: " + repr(drift))


def run(args: argparse.Namespace) -> None:
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0):  # PR_SET_CHILD_SUBREAPER: reap owned fatal-test descendants.
        raise OSError(ctypes.get_errno(), "Cannot become fixture subreaper")
    binary = args.binary.resolve()
    inputs = {str(path): owner.sha256(path) for path in (
        binary, Path(__file__).resolve(), Path(owner.__file__).resolve())}
    if args.baseline_binary:
        inputs[str(args.baseline_binary.resolve())] = owner.sha256(args.baseline_binary.resolve())
    owner.write_json(out / "inputs-before.json", inputs)
    common = ["--type=gpu-process", "--use-webgpu-runtime=doe", "--ozone-platform=x11", "--expect-selected"]
    cases = []
    for mode in ("default", "scoped"):
        for operation in ("self-main", "self-worker", "sibling", "foreign"):
            flags = [*common, "--policy=" + mode, "--operation=" + operation]
            cases.append((mode + "-" + operation, binary, flags,
                          mode == "scoped" or operation.startswith("self")))
    cases.append(("scoped-affinity-sibling", binary,
                  [*common, "--policy=scoped", "--operation=affinity-sibling"], False))
    for name, flags in (
        ("native-dawn", ["--use-webgpu-runtime=dawn", "--ozone-platform=x11"]),
        ("wayland", ["--use-webgpu-runtime=doe", "--ozone-platform=wayland"]),
        ("vulkan", ["--use-webgpu-runtime=doe", "--ozone-platform=x11", "--use-angle=vulkan"]),
        ("software", ["--use-webgpu-runtime=doe", "--ozone-platform=x11", "--use-angle=swiftshader"]),
        ("disabled", ["--use-webgpu-runtime=doe", "--ozone-platform=x11", "--disable-webgpu-doe"]),
        ("late", ["--use-webgpu-runtime=doe", "--ozone-platform=x11", "--late-policy"]),
    ):
        cases.append((name, binary, ["--type=gpu-process", *flags,
                                    "--policy=scoped", "--operation=sibling"], False))
    if args.baseline_binary:
        for operation in ("self-main", "self-worker", "sibling", "foreign"):
            cases.append(("baseline-" + operation, args.baseline_binary.resolve(),
                          [*common, "--policy=default", "--operation=" + operation],
                          operation.startswith("self")))
    rows = []
    try:
        for name, executable, flags, success in cases:
            command = [str(executable), *flags]
            started = time.monotonic()
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       start_new_session=True)
            timed_out = False
            try:
                stdout, stderr = process.communicate(timeout=15)
            except subprocess.TimeoutExpired:
                timed_out = True
                os.killpg(process.pid, signal.SIGKILL)
                stdout, stderr = process.communicate(timeout=3)
            (out / (name + ".stdout")).write_bytes(stdout)
            (out / (name + ".stderr")).write_bytes(stderr)
            reaped = []
            deadline = time.monotonic() + 5
            while True:
                try:
                    pid, status = os.waitpid(-process.pid, os.WNOHANG)
                except ChildProcessError:
                    break
                if pid:
                    reaped.append({"pid": pid, "status": status})
                elif time.monotonic() > deadline:
                    os.killpg(process.pid, signal.SIGKILL)
                    raise RuntimeError("Owned CPU fixture descendant did not exit")
                else:
                    time.sleep(.01)
            expected_syscall = re.search(rb"EXPECT_DENIED_SYSCALL (nr=0x[0-9a-f]+)", stdout)
            matched = (process.returncode == 0 and b"PASS seccomp=2" in stdout) if success else (
                process.returncode < 0 and b"**CRASHING**" in stderr and b"SEALED" in stdout
                and expected_syscall is not None and expected_syscall[1] + b" " in stderr)
            rows.append({"id": name, "command": command, "expectedSuccess": success,
                         "returnCode": process.returncode, "timedOut": timed_out,
                         "matched": matched and not timed_out,
                         "elapsedSeconds": time.monotonic() - started,
                         "ownedDescendantsReaped": reaped})
            owner.write_json(out / "results.json", rows)
            if not rows[-1]["matched"]:
                raise RuntimeError("CPU BPF control rejected: " + name)
        owner.write_json(out / "verdict.json", {"passed": True, "cases": len(rows),
                         "scope": "Actual GPU seccomp CPU controls, no GL/provider/GPU work"})
    finally:
        drift = [path for path, digest in inputs.items() if owner.sha256(Path(path)) != digest]
        owner.write_json(out / "source-verdict.json", {"sourceDrift": drift})
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
    run_parser.add_argument("--baseline-binary", type=Path)
    run_parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    (build if args.operation == "build" else run)(args)


if __name__ == "__main__":
    main()
