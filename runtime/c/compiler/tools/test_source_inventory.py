# SPDX-License-Identifier: MIT
import unittest
from pathlib import Path
from source_inventory import compiler_sources, target_names


class SourceInventoryTests(unittest.TestCase):
    def test_append_and_continuation(self):
        self.assertEqual(target_names("# owner\n N = a.c \\\n b.c\n\nN += c.c # added\n", "N"),
                         ["a.c", "b.c", "c.c"])

    def test_reject_ambiguous_or_unsafe(self):
        for source in ("N += a.c", "N = a.c\nN = b.c", "N = a.c\nN += a.c",
                       "N = ../a.c", "N = $(shell echo x)", "N = a.c\nX = b.c",
                       "N = a.c \\", "N ="):
            with self.subTest(source=source), self.assertRaises(ValueError):
                target_names(source, "N")

    def test_actual_inventory_and_producers(self):
        root = Path(__file__).resolve().parents[1]
        paths = compiler_sources(root)
        for target in ("metal", "hlsl"):
            for name in ("float_pack.c", "subgroup.c"):
                self.assertIn(root / "targets" / target / name, paths)
        from build_windows import source_paths
        from build_browser import source_files, ADAPTERS
        self.assertEqual(source_paths(), paths)
        self.assertEqual(source_files(), paths + [root / name for name in ADAPTERS])


if __name__ == "__main__":
    unittest.main()
