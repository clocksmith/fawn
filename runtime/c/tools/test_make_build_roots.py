"""Evaluate owner Make paths without compiling or mixing checkout artifacts."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
OWNERS = ("futures", "callbacks", "instance", "error-scopes", "shader")


@unittest.skipUnless(shutil.which("make"), "make is unavailable")
class BuildRootTests(unittest.TestCase):
    def paths(self, root: Path, owner: str, *overrides: str) -> dict[str, str]:
        probe = ".PHONY: print-paths\nprint-paths:\n"
        for name in ("DOE_C_TMP", "BUILD", "FUTURES_ARCHIVE", "COMPILER_LIBRARY"):
            probe += f"\t@printf '{name}=%s\\n' '$({name})'\n"
        environment = os.environ.copy()
        for name in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "DOE_C_TMP", "BUILD", "FUTURES_ARCHIVE",
                     "COMPILER_LIBRARY"):
            environment.pop(name, None)
        command = ["make", "--no-print-directory", "-C", str(root / "runtime/c" / owner),
                   "-f", "Makefile", "-f", "-", "print-paths", *overrides]
        result = subprocess.run(command, input=probe, text=True, capture_output=True,
                                env=environment, timeout=30, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return dict(line.split("=", 1) for line in result.stdout.splitlines())

    def test_shared_scratch_root_keeps_dependencies_with_producer(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            scratch = Path(temporary) / "objects"
            for owner in OWNERS:
                with self.subTest(owner=owner):
                    paths = self.paths(ROOT, owner, f"DOE_C_TMP={scratch}")
                    self.assertEqual(paths["BUILD"], str(scratch / owner))
                    if owner in ("callbacks", "instance"):
                        self.assertEqual(paths["FUTURES_ARCHIVE"], str(scratch / "futures/libdoe_futures.a"))
                    if owner == "shader":
                        self.assertEqual(paths["COMPILER_LIBRARY"], "../compiler/build/libdoe_compiler.so")

    def test_explicit_output_and_dependency_remain_independent(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            scratch = Path(temporary)
            for owner in OWNERS:
                with self.subTest(owner=owner):
                    paths = self.paths(ROOT, owner, f"BUILD={scratch / 'selected'}",
                                       f"FUTURES_ARCHIVE={scratch / 'selected-futures.a'}")
                    self.assertEqual(paths["BUILD"], str(scratch / "selected"))
                    self.assertEqual(paths["FUTURES_ARCHIVE"], str(scratch / "selected-futures.a"))

    def test_checkout_identity_separates_default_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            roots = [Path(temporary) / name for name in ("first", "second")]
            defaults = []
            for root in roots:
                c_root = root / "runtime/c"
                c_root.mkdir(parents=True)
                shutil.copyfile(ROOT / "runtime/c/build-root.mk", c_root / "build-root.mk")
                for owner in OWNERS:
                    destination = c_root / owner
                    destination.mkdir()
                    shutil.copyfile(ROOT / "runtime/c" / owner / "Makefile", destination / "Makefile")
                backend = c_root / "backends/vulkan"
                backend.mkdir(parents=True)
                shutil.copyfile(ROOT / "runtime/c/backends/vulkan/Makefile", backend / "Makefile")
                paths = {owner: self.paths(root, owner) for owner in OWNERS}
                scratch_roots = {value["DOE_C_TMP"] for value in paths.values()}
                self.assertEqual(len(scratch_roots), 1)
                for owner, value in paths.items():
                    self.assertEqual(value["BUILD"], str(Path(value["DOE_C_TMP"]) / owner))
                defaults.append(scratch_roots.pop())
            self.assertNotEqual(defaults[0], defaults[1])


if __name__ == "__main__":
    unittest.main()
