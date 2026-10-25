"""Exercise the C offline adapter's source and atomic file-publication contract."""
from pathlib import Path
import subprocess
import tempfile
import unittest

COMPILER = Path(__file__).resolve().parents[1]
BINARY = COMPILER / "build/doe-emit-wgsl-c"


class OfflineWgslTests(unittest.TestCase):
    def test_source_boundaries_and_failed_output_preservation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / "input.wgsl", root / "output.wgsl"
            for text in ("", " " * 65536, "fn f(x:u32)->u32{return x/8u;}"):
                source.write_text(text)
                result = subprocess.run([BINARY, source, output], capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                expected = text.replace("x/8u", "(x>> 3u)")
                self.assertEqual(output.read_text(), expected)
            output.write_text("retained output")
            for text in ("fn alias() {}", " " * 65537):
                source.write_text(text)
                result = subprocess.run([BINARY, source, output], capture_output=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(output.read_text(), "retained output")
            self.assertEqual(sorted(p.name for p in root.iterdir()), ["input.wgsl", "output.wgsl"])

    def test_output_failure_cleans_only_owned_temporary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.wgsl"
            source.write_text("fn f(){}")
            output = root / "directory.wgsl"
            output.mkdir()
            (output / "sentinel").write_text("preserve")
            result = subprocess.run([BINARY, source, output], capture_output=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((output / "sentinel").read_text(), "preserve")
            self.assertEqual(sorted(p.name for p in root.iterdir()), ["directory.wgsl", "input.wgsl"])
            missing = root / "absent/output.wgsl"
            result = subprocess.run([BINARY, source, missing], capture_output=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(missing.exists())

    def test_help(self):
        result = subprocess.run([BINARY, "--help"], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0)
        self.assertIn(b"SOURCE.wgsl OUTPUT.wgsl", result.stdout)


if __name__ == "__main__":
    unittest.main()
