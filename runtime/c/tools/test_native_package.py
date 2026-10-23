#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Non-destructive preflight controls; actual relocation has a separate receipt."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("native_package", Path(__file__).with_name("build-native-package.py"))
assert SPEC is not None and SPEC.loader is not None
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class PackagePreflightTests(unittest.TestCase):
    def test_existing_paths_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            occupied = root / "occupied"
            occupied.mkdir()
            sentinel = occupied / "source"
            sentinel.write_text("preserve")
            for build, destination in ((occupied, root / "new"), (root / "new", occupied)):
                with self.assertRaises(FileExistsError):
                    PACKAGE.build_package(root, "HEAD", build, destination, "gcc", "g++")
                self.assertEqual(sentinel.read_text(), "preserve")
                self.assertFalse((root / "new").exists())

    def test_overlapping_paths_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "fresh"
            for destination in (build, build / "child"):
                with self.assertRaises(ValueError):
                    PACKAGE.build_package(root, "HEAD", build, destination, "gcc", "g++")
                self.assertFalse(build.exists())

    def test_unsupported_host_has_no_side_effects(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.object(PACKAGE.platform, "system", return_value="Darwin"):
            root = Path(directory)
            with self.assertRaises(ValueError):
                PACKAGE.build_package(root, "HEAD", root / "build", root / "stage", "gcc", "g++")
            self.assertEqual(list(root.iterdir()), [])

    def test_missing_revision_precedes_path_creation(self) -> None:
        import subprocess
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(PACKAGE, "captured", side_effect=subprocess.CalledProcessError(128, "git")):
                with self.assertRaises(subprocess.CalledProcessError):
                    PACKAGE.build_package(root, "missing", root / "build", root / "stage", "gcc", "g++")
            self.assertEqual(list(root.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
