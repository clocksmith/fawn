#!/usr/bin/env python3
"""Compiled-object tests for the mutable-global ownership check."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from check_globals import main

SOURCE = b"""
static int counter;
int exported_state = 3;
static const char *const names[] = {"a", "b"};
static const int table[] = {1, 2, 3};
const char *const *use(int i) { counter += i; return names + table[i]; }
"""


@unittest.skipUnless(shutil.which("cc") and shutil.which("objdump"), "needs cc and objdump")
class GlobalsTests(unittest.TestCase):
    def run_check(self, allow_lines: str) -> int:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "unit.c").write_bytes(SOURCE)
            subprocess.run(["cc", "-O2", "-fPIC", "-c", str(root / "unit.c"), "-o", str(root / "unit.o")],
                           check=True)
            allow = root / "allow.txt"
            allow.write_text(allow_lines)
            return main([str(root / "unit.o"), "--allow", str(allow)])

    def test_unowned_mutable_state_fails(self):
        self.assertEqual(self.run_check("counter src/unit.c test counter\n"), 1)

    def test_owned_state_passes_and_constants_are_ignored(self):
        allow = "counter src/unit.c test counter\nexported_state src/unit.c test state\n"
        self.assertEqual(self.run_check(allow), 0)

    def test_stale_entry_fails(self):
        allow = ("counter src/unit.c test counter\nexported_state src/unit.c test state\n"
                 "removed_state src/unit.c no longer defined\n")
        self.assertEqual(self.run_check(allow), 1)


if __name__ == "__main__":
    unittest.main()
