from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STAGER = ROOT / "browser/chromium/scripts/stage-app-runtime.mjs"


class BrowserRuntimeBundleTests(unittest.TestCase):
    def test_updates_preserve_mapped_bytes_and_reuse_only_exact_complete_closures(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "Fawn with spaces.app"
            sources = root / "source"
            sources.mkdir()
            runtime = sources / "libwebgpu_doe_c.dylib"
            compiler = sources / "libdoe_compiler.dylib"
            runtime.write_bytes(b"first runtime")
            compiler.write_bytes(b"first compiler")

            def stage() -> subprocess.CompletedProcess[str]:
                return subprocess.run(
                    ["node", str(STAGER), "--app", str(app), "--library", str(runtime)],
                    cwd=ROOT, text=True, capture_output=True, timeout=10,
                )

            first = stage()
            self.assertEqual(first.returncode, 0, first.stderr)
            accepted = Path(first.stdout.strip())
            old_inode = accepted.stat().st_ino
            with accepted.open("rb") as mapped:
                self.assertEqual(stage().stdout, first.stdout)
                runtime.write_bytes(b"second runtime")
                compiler.write_bytes(b"second compiler")
                second = stage()
                self.assertEqual(second.returncode, 0, second.stderr)
                candidate = Path(second.stdout.strip())
                self.assertNotEqual(candidate, accepted)
                self.assertEqual(mapped.read(), b"first runtime")
                self.assertEqual(accepted.stat().st_ino, old_inode)
                self.assertEqual(accepted.read_bytes(), b"first runtime")
                self.assertEqual(candidate.read_bytes(), b"second runtime")
                self.assertEqual((candidate.parent / compiler.name).read_bytes(), b"second compiler")
            compiler.unlink()
            self.assertNotEqual(stage().returncode, 0)
            self.assertEqual(candidate.read_bytes(), b"second runtime")
            compiler.write_bytes(b"second compiler")
            candidate.write_bytes(b"tampered runtime")
            failed = stage()
            self.assertNotEqual(failed.returncode, 0)
            self.assertIn("does not match", failed.stderr)


if __name__ == "__main__":
    unittest.main()
