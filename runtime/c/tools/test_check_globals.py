#!/usr/bin/env python3
"""Compiled-object tests for the mutable-global ownership check."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from check_globals import main, writable_names

SOURCE = b"""
static int counter;
int exported_state = 3;
static const char *const names[] = {"a", "b"};
static const int table[] = {1, 2, 3};
const char *const *use(int i) { counter += i; return names + table[i]; }
"""


class SymbolFormatTests(unittest.TestCase):
    def test_elf_writable_objects_exclude_constants_and_functions(self) -> None:
        symbols = """
0000000000000000 l     O .bss 0000000000000004 counter
0000000000000000 g     O .data 0000000000000004 exported_state
0000000000000000 l     O .data.rel.ro.local 0000000000000010 names
0000000000000000 l     O .rodata 000000000000000c table
0000000000000000 g     F .text 0000000000000020 use
0000000000000000 l     O .bss 0000000000000001 completed.0
"""
        self.assertEqual(writable_names(symbols), {"counter", "exported_state"})

    def test_macho_normalizes_one_prefix_without_accepting_section_labels(self) -> None:
        symbols = """
0000000000000070 l     O __DATA,__bss _counter
000000000000002c g     O __DATA,__data _exported_state
0000000000000030 l     O __DATA,__const _names
0000000000000040 l     O __TEXT,__const _table
0000000000000070 l     O __DATA,__bss ltmp2
0000000000000074 l     O __DATA,__common __private_state
0000000000000000 g     F __TEXT,__text _use
"""
        self.assertEqual(writable_names(symbols),
                         {"counter", "exported_state", "_private_state"})

    def test_macho_thread_local_storage_remains_owned_mutable_state(self) -> None:
        symbols = """
0000000000000000 g     O __DATA,__thread_data _thread_data
0000000000000004 l     O __DATA,__thread_bss _thread_bss
0000000000000008 g     F __DATA,__data _function
"""
        self.assertEqual(writable_names(symbols), {"thread_data", "thread_bss"})


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
