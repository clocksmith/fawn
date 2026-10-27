"""Actual Xlib/XCB resource-ordering controls; no GL or GPU execution."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


HERE = Path(__file__).resolve().parent


@unittest.skipUnless(sys.platform == "linux" and os.environ.get("DISPLAY"),
                     "requires Linux seccomp and an authorized X display")
class X11PreSandboxTests(unittest.TestCase):
    def test_existing_connections_survive_without_permitting_new_connections(self) -> None:
        source = HERE / "linux_x11_presandbox_controls.c"
        with tempfile.TemporaryDirectory(prefix="fawn-x11-order-") as temporary:
            binary = Path(temporary) / "control"
            command = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                       str(source), "-lX11", "-lxcb", "-o", str(binary)]
            compiled = subprocess.run(command, text=True, capture_output=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            for order in ["seal-before-prepare", "prepare-before-seal"]:
                ran = subprocess.run([str(binary), order], text=True, capture_output=True, timeout=15)
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
                self.assertIn("no GL/GPU", ran.stdout)
                print(ran.stdout, end="")
            retained = os.environ.get("FAWN_X11_ORDER_CONTROL_OUT")
            if retained:
                out = Path(retained)
                out.mkdir(parents=True, exist_ok=False)
                shutil.copy2(binary, out / binary.name)
                shutil.copy2(source, out / source.name)
                record = {"scope": "Real X11 metadata ordering under a process-local socket/connect barrier; not Chromium sandbox or GPU qualification",
                          "sourceSha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                          "binarySha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                          "command": command, "display": os.environ["DISPLAY"]}
                (out / "result.json").write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    unittest.main()
