#!/usr/bin/env python3
"""Boundary tests for the source-size invariant, not a code-quality score."""
from pathlib import Path
import tempfile
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
