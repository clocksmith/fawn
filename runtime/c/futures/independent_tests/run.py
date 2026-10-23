#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Independent source-only CPU futures evaluation; never invokes Zig."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
CFLAGS = ["-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra",
          "-Werror", "-pedantic", "-pthread", "-fno-common"]
WRAPS = {
    "concurrency": ["pthread_cond_wait", "pthread_cond_timedwait"],
    "init_faults": ["pthread_mutex_init", "pthread_condattr_init",
                    "pthread_condattr_setclock", "pthread_cond_init",
                    "pthread_condattr_destroy", "pthread_cond_destroy",
                    "pthread_mutex_destroy"],
    "wait_faults": ["clock_gettime", "pthread_cond_wait",
                    "pthread_cond_timedwait"],
}
TESTS = ["functional", "concurrency", "init_faults", "wait_faults",
         "selection_reuse", "consumer_c"]
SYMBOLS = sorted("doe_future_" + name for name in [
    "queue_requirements", "completion_requirements", "queue_init",
    "completion_init", "queue_deinit", "completion_deinit", "new_id",
    "legacy_id", "register", "begin_delivery", "mark_ready", "finish",
    "process_events", "wait_any"])


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def manifest(paths: list[Path], root: Path) -> dict[str, str]:
    return {str(path.relative_to(root)): digest(path) for path in sorted(paths)}


class Campaign:
    def __init__(self, source: Path, output: Path,
                 source_required: bool = True) -> None:
        self.source = source
        self.output = output
        self.include = source / "include"
        self.sources = sorted((source / "src").glob("*.c"))
        if source_required and not self.sources:
            raise FileNotFoundError(f"No library sources under {source / 'src'}")
        self.records: list[dict[str, object]] = []
        self.skips: list[str] = []
        self.failures: list[str] = []
        self.output.mkdir(parents=True, exist_ok=False)
        self.source_manifest = manifest(
            self.sources + sorted((self.source / "src").glob("*.h")) + sorted(self.include.glob("*.h")) +
            [p for p in [source / "CONTRACT.md", source / "PROVENANCE.md"]
             if p.is_file()], source)
        self.suite_manifest = manifest(
            [p for p in HERE.iterdir() if p.is_file()], HERE)

    def run(self, name: str, command: list[str], expected: int = 0,
            optional: bool = False) -> bool:
        started = time.monotonic()
        try:
            result = subprocess.run(command, text=True, capture_output=True,
                                    timeout=45, check=False)
            code, text = result.returncode, result.stdout + result.stderr
        except subprocess.TimeoutExpired as error:
            code = 124
            stdout = error.stdout or b""
            stderr = error.stderr or b""
            if isinstance(stdout, bytes):
                stdout = stdout.decode("utf-8", errors="replace")
            if isinstance(stderr, bytes):
                stderr = stderr.decode("utf-8", errors="replace")
            text = stdout + stderr + "\nHARNESS TIMEOUT\n"
        log = self.output / f"{len(self.records):03d}-{name}.log"
        log.write_text(text, encoding="utf-8")
        passed = code == expected if expected == 0 else code != 0
        self.records.append({"name": name, "command": command,
                             "exit_code": code, "expected": expected,
                             "passed": passed, "optional": optional,
                             "log": log.name,
                             "elapsed_seconds": time.monotonic() - started})
        if not passed and not optional:
            self.failures.append(name)
        print(f"{'PASS' if passed else 'FAIL'} {name} exit={code}", flush=True)
        self.save()
        return passed

    def save(self) -> None:
        data = {"schema": 1, "source_root": str(self.source),
                "source_manifest": self.source_manifest,
                "suite_manifest": self.suite_manifest,
                "commands": self.records, "failures": self.failures,
                "unsupported_runtime": self.skips,
                "no_zig_invoked": True, "leak_sanitizer": "not claimed; detect_leaks=0 under ptrace",
                "scope": "CPU queue only, no production/GPU/lease qualification"}
        (self.output / "results.json").write_text(
            json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    def archive(self, name: str, flags: list[str]) -> Path | None:
        directory = self.output / name
        directory.mkdir()
        objects = []
        for index, source in enumerate(self.sources):
            obj = directory / f"{index}-{source.stem}.o"
            command = ["gcc", *CFLAGS, *flags, "-I", str(self.include),
                       "-c", str(source), "-o", str(obj)]
            if not self.run(f"{name}-compile-{source.stem}", command):
                return None
            objects.append(str(obj))
        archive = directory / "libdoe_futures.a"
        if not self.run(f"{name}-archive", ["ar", "rcs", str(archive), *objects]):
            return None
        return archive

    def c_test(self, name: str, archive: Path, flags: list[str],
               prefix: str) -> None:
        binary = self.output / f"{prefix}-{name}"
        command = ["gcc", *CFLAGS, *flags, "-I", str(self.include),
                   str(HERE / f"{name}.c"), str(archive)]
        command += [f"-Wl,--wrap={symbol}" for symbol in WRAPS.get(name, [])]
        command += ["-o", str(binary)]
        if self.run(f"{prefix}-{name}-build", command):
            self.run(f"{prefix}-{name}-run", (["env", "ASAN_OPTIONS=detect_leaks=0"] if prefix == "asan-ubsan" else []) + [str(binary)])

    def cpp(self, archive: Path, prefix: str) -> None:
        obj = self.output / f"{prefix}-abi.o"
        if not self.run(f"{prefix}-c-layout", [
            "gcc", *CFLAGS, "-O2", "-I", str(self.include), "-c",
            str(HERE / "abi.c"), "-o", str(obj)]):
            return
        binary = self.output / f"{prefix}-consumer-cpp"
        command = ["g++", "-std=c++17", "-O2", "-Wall", "-Wextra",
                   "-Werror", "-pedantic", "-fno-exceptions", "-fno-rtti",
                   "-pthread", "-I", str(self.include),
                   str(HERE / "consumer.cpp"), str(obj), str(archive),
                   "-o", str(binary)]
        if self.run(f"{prefix}-cpp-build", command):
            self.run(f"{prefix}-cpp-run", [str(binary)])

    def audit(self, archive: Path) -> None:
        for mode in ["--defined-only", "--undefined-only"]:
            self.run(f"archive-nm-{mode[2:]}", ["nm", "-g", mode, str(archive)])
        defined = (self.output / self.records[-2]["log"]).read_text(encoding="utf-8")
        undefined = (self.output / self.records[-1]["log"]).read_text(encoding="utf-8")
        actual = sorted(set(re.findall(r"\b(doe_future_\w+)\b", defined)))
        forbidden = re.findall(r"\b(?:malloc|calloc|realloc|free|aligned_alloc|posix_memalign|\w*zig\w*)\b", undefined, flags=re.I)
        audit = {"archive_sha256": digest(archive), "expected_exports": SYMBOLS,
                 "actual_exports": actual, "forbidden_undefined": forbidden,
                 "passed": actual == SYMBOLS and not forbidden}
        (self.output / "archive_audit.json").write_text(
            json.dumps(audit, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if not audit["passed"]:
            self.failures.append("archive-symbol-allocation-zig-audit")
        self.save()

    def sanitize(self, name: str, flags: list[str], tests: list[str]) -> None:
        probe = self.output / f"{name}-runtime-probe"
        if not self.run(f"{name}-probe-build", ["gcc", *CFLAGS, *flags,
                        str(HERE / "sanitizer_probe.c"), "-o", str(probe)],
                        optional=True):
            self.skips.append(f"{name}: runtime probe build unsupported")
            self.save()
            return
        if not self.run(f"{name}-probe-run", (["env", "ASAN_OPTIONS=detect_leaks=0"] if name == "asan-ubsan" else []) + [str(probe)], optional=True):
            self.skips.append(f"{name}: runtime probe execution failed")
            self.save()
            return
        archive = self.archive(name, flags)
        if archive:
            for test in tests:
                self.c_test(test, archive, flags, name)

    def full(self) -> None:
        self.run("toolchain-gcc", ["gcc", "--version"])
        self.run("toolchain-gpp", ["g++", "--version"])
        self.run("platform", ["uname", "-a"])
        archive = self.archive("optimized", ["-O2", "-g"])
        if not archive:
            return
        self.audit(archive)
        for test in TESTS:
            self.c_test(test, archive, ["-O2", "-g"], "optimized")
        self.cpp(archive, "optimized")
        limits = ["-DDOE_FUTURES_TESTING", "-DDOE_FUTURES_TEST_INSTANCE_LIMIT=2",
                  "-DDOE_FUTURES_TEST_SEQUENCE_LIMIT=3",
                  "-DDOE_FUTURES_TEST_LEGACY_LIMIT=0x8000000000000002ULL"]
        small = self.archive("tiny-limits", ["-O2", *limits])
        if small:
            self.c_test("exhaustion", small, ["-O2"], "tiny-limits")
        self.run("ungated-limit-override-rejected", [
            "gcc", *CFLAGS, "-I", str(self.include),
            "-DDOE_FUTURES_TEST_INSTANCE_LIMIT=2", "-fsyntax-only",
            str(self.sources[0])], expected=1)
        self.sanitize("asan-ubsan", ["-O1", "-g", "-fsanitize=address,undefined",
                      "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"], TESTS[:5])
        self.sanitize("tsan", ["-O1", "-g", "-fsanitize=thread",
                      "-fno-omit-frame-pointer"], ["concurrency", "selection_reuse"])
        if self.source_manifest != manifest(
            self.sources + sorted((self.source / "src").glob("*.h")) + sorted(self.include.glob("*.h")) +
            [self.source / "CONTRACT.md", self.source / "PROVENANCE.md"], self.source):
            self.failures.append("candidate-source-changed-during-campaign")
        self.save()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Frozen runtime/c/futures directory")
    parser.add_argument("--output", type=Path,
                        help="New nonexisting evidence output directory")
    parser.add_argument("--mode", choices=["plan", "full", "smoke"], default="plan",
                        help="Full once, or packaging plan/smoke")
    parser.add_argument("--archive", type=Path,
                        help="Exact independently built archive for smoke")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.mode == "plan":
        print(json.dumps({"source_root": str(args.source_root),
                          "tests": TESTS + ["consumer.cpp", "exhaustion"],
                          "sanitizers": ["asan-ubsan", "tsan"],
                          "requires": ["gcc", "g++", "ar", "nm", "pthread"],
                          "zig": "never invoked, linked, or executed"}, indent=2))
        return 0
    if not args.output:
        raise ValueError("--output is required for full or smoke")
    campaign = Campaign(args.source_root.resolve(), args.output.resolve(),
                        source_required=args.mode == "full")
    if args.mode == "full":
        campaign.full()
    else:
        if not args.archive or not args.archive.is_file():
            raise FileNotFoundError("--archive must identify the existing frozen archive")
        campaign.c_test("consumer_c", args.archive.resolve(), ["-O2"], "smoke")
        campaign.cpp(args.archive.resolve(), "smoke")
        campaign.audit(args.archive.resolve())
    campaign.save()
    print(json.dumps({"failures": campaign.failures,
                      "unsupported_runtime": campaign.skips}, indent=2))
    return 1 if campaign.failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
