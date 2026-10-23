#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise incremental callback test linking against a changed futures archive."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time
import unittest


CALLBACKS = Path(__file__).resolve().parents[1]
FUTURES = CALLBACKS.parent / "futures"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_make(owner: Path, *arguments: str) -> None:
    command = ["make", "--no-print-directory", "-C", str(owner), *arguments]
    environment = os.environ.copy()
    # Inherited forced builds or NDEBUG could invalidate the regression oracle.
    for name in (
        "MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "CFLAGS", "CXXFLAGS",
        "CPPFLAGS", "LDFLAGS", "LDLIBS",
    ):
        environment.pop(name, None)
    print("$ " + shlex.join(command), flush=True)
    subprocess.run(command, env=environment, check=True, timeout=60)


class TestBuildDependencies(unittest.TestCase):
    makefile = CALLBACKS / "Makefile"
    output_dir: Path | None = None

    def setUp(self) -> None:
        if self.output_dir is None:
            temporary = tempfile.TemporaryDirectory(prefix="doe-callback-link-")
            self.addCleanup(temporary.cleanup)
            self.output = Path(temporary.name)
        else:
            self.output = self.output_dir
            self.output.mkdir(parents=True, exist_ok=False)
        print(f"Build output: {self.output}", flush=True)

    def snapshot(self, build: Path, phase: str) -> dict[str, tuple[int, str]]:
        result = {
            name: ((build / name).stat().st_mtime_ns, digest(build / name))
            for name in (
                "test_api", "test_cpp", "callbacks.o", "libdoe_callbacks.a",
            )
        }
        print(f"{phase}: {json.dumps(result, sort_keys=True)}", flush=True)
        return result

    def build_futures(self, name: str, optimization: str) -> Path:
        build = self.output / name
        run_make(
            FUTURES, f"BUILD={build}", "CC=gcc", "AR=ar",
            f"CFLAGS={optimization} -g -std=c11 -Wall -Wextra "
            "-Wpedantic -Werror -pthread", "all",
        )
        return build / "libdoe_futures.a"

    def check_callbacks(self, build: Path, selected: Path) -> None:
        run_make(
            CALLBACKS, "-f", str(self.makefile), f"BUILD={build}",
            f"FUTURES_ARCHIVE={selected}", "CC=gcc", "CXX=g++", "AR=ar",
            "check",
        )

    def test_changed_futures_archive_relinks_both_consumers(self) -> None:
        selected = self.output / "selected-futures.a"
        shutil.copyfile(self.build_futures("futures-o0", "-O0"), selected)
        original_digest = digest(selected)
        build = self.output / "callbacks"
        self.check_callbacks(build, selected)
        initial = self.snapshot(build, "initial")

        self.check_callbacks(build, selected)
        self.assertEqual(initial, self.snapshot(build, "initial no-op"))

        # Same actual owner sources and ABI, different object bytes. This is not
        # an unchanged archive touch or a stub replacing the real C owner.
        replacement = self.build_futures("futures-o2", "-O2")
        self.assertNotEqual(original_digest, digest(replacement))
        shutil.copyfile(replacement, selected)
        newest_consumer = max(initial[name][0] for name in ("test_api", "test_cpp"))
        timestamp = max(time.time_ns(), newest_consumer + 1)
        os.utime(selected, ns=(timestamp, timestamp))
        self.assertGreater(selected.stat().st_mtime_ns, newest_consumer)
        self.assertNotEqual(original_digest, digest(selected))
        print(
            f"Selected archive changed: {original_digest} -> {digest(selected)}; "
            f"mtime_ns={selected.stat().st_mtime_ns}", flush=True,
        )

        # check runs the existing C/C++ assertion-bearing tests after relinking.
        self.check_callbacks(build, selected)
        rebuilt = self.snapshot(build, "after archive replacement")
        for name in ("test_api", "test_cpp"):
            with self.subTest(consumer=name):
                self.assertGreater(
                    rebuilt[name][0], initial[name][0], f"{name} did not relink",
                )
                self.assertNotEqual(
                    rebuilt[name][1], initial[name][1],
                    f"{name} did not consume the byte-different futures owner",
                )
        for name in ("callbacks.o", "libdoe_callbacks.a"):
            self.assertEqual(initial[name], rebuilt[name], name)

        self.check_callbacks(build, selected)
        self.assertEqual(rebuilt, self.snapshot(build, "final no-op"))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--makefile", type=Path, default=CALLBACKS / "Makefile",
        help="Callback Makefile to exercise; permits a preserved negative control.",
    )
    parser.add_argument(
        "--output-dir", type=Path,
        help="Fresh directory to retain builds; otherwise use temporary output.",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    TestBuildDependencies.makefile = arguments.makefile.resolve(strict=True)
    if arguments.output_dir is not None:
        TestBuildDependencies.output_dir = arguments.output_dir.resolve()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(TestBuildDependencies)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
