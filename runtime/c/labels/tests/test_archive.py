#!/usr/bin/env python3
"""Developer regression: actual default-PIE objects are not shared-library PIC."""
from __future__ import annotations

import os
from pathlib import Path
import runpy
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[4]
OWNER = ROOT / "runtime/c/labels"


class TestArchivePic(unittest.TestCase):
    def test_external_pc32_is_rejected_before_native_link(self) -> None:
        producer = runpy.run_path(str(OWNER / "tools/archive.py"))
        settings = producer["inputs"](ROOT, "gcc", "ar")
        build = Path(os.environ["DOE_LABELS_TEST_OUTPUT"]).resolve()
        build.mkdir(parents=True, exist_ok=True)
        compiler = settings["tools"]["cc"]["path"]
        common = [compiler, *[flag for flag in producer["FLAGS"] if flag != "-fPIC"],
                  "-I", str(OWNER / "include"), "-c", str(OWNER / "src/store.c")]
        for profile, option in (("pic", "-fPIC"), ("pie", "-fPIE")):
            output = build / (profile + ".o")
            command = [*common, option, "-o", str(output)]
            result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, check=False)
            self.assertEqual(result.returncode, 0, result.stdout.decode())
            if profile == "pic":
                producer["check_object"](output, "store", ROOT, settings)
            else:
                with self.assertRaisesRegex(ValueError, "non-PIC external PC32"):
                    producer["check_object"](output, "store", ROOT, settings)
                link = subprocess.run([compiler, "-shared", str(output), "-o",
                                       str(build / "must-not-link.so")], cwd=ROOT,
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                self.assertNotEqual(link.returncode, 0)
                self.assertIn(b"R_X86_64_PC32", link.stdout)


if __name__ == "__main__":
    unittest.main()
