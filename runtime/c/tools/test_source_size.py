#!/usr/bin/env python3
"""Boundary tests for the source-size invariant, not a code-quality score."""
from pathlib import Path
import tempfile
import subprocess
import sys
import unittest

from check_source_size import check


class SourceSizeTests(unittest.TestCase):
    def test_physical_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "unit.c"
            for data, passes in ((b"", True), (b"\n" * 999, True),
                                 (b"x\r\n" * 999, True),
                                 (b"x\n" * 998 + b"last", True),
                                 (b"\n" * 1000, False),
                                 (b"x\n" * 999 + b"last", False)):
                path.write_bytes(data)
                self.assertEqual(check(path) is None, passes)

    def test_explicit_non_source_argument_fails(self):
        script = Path(__file__).with_name("check_source_size.py")
        with tempfile.TemporaryDirectory() as directory:
            text = Path(directory) / "note.txt"
            text.write_text("not source")
            for argument in (directory, str(text)):
                result = subprocess.run([sys.executable, str(script), argument],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("expected a C/header file", result.stderr)

    def test_missing_and_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "unit.h"
            self.assertIsNotNone(check(path))
            target = Path(directory) / "target.h"
            target.write_bytes(b"int value;\n")
            path.symlink_to(target)
            self.assertIsNotNone(check(path))


if __name__ == "__main__":
    unittest.main()
