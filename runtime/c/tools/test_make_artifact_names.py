"""Native artifact facts survive GNU Make directory-printing recursion."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]


@unittest.skipUnless(shutil.which("make"), "make is unavailable")
class ArtifactNameTests(unittest.TestCase):
    def test_direct_platform_fact_with_directory_printing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            scratch = Path(temporary)
            for platform, suffix in (("Linux", "so"), ("Darwin", "dylib")):
                for owner, variable, expected_prefix in (
                    ("native", "NATIVE_LIBRARY", scratch / "build"),
                    ("runtime-cli", "NATIVE_LIBRARY", scratch / "native"),
                    ("", "STAGED_NATIVE_LIBRARY", scratch / "stage" / "lib"),
                ):
                    with self.subTest(platform=platform, owner=owner):
                        probe = scratch / "probe.mk"
                        probe.write_text(
                            ".PHONY: print-artifact-fact\n"
                            "print-artifact-fact:\n"
                            f"\t@printf 'ARTIFACT=%s\\n' '$({variable})'\n",
                            encoding="utf-8",
                        )
                        command = [
                            "make", "-w", "-C", str(ROOT / "runtime/c" / owner),
                            "-f", "Makefile", "-f", str(probe),
                            "print-artifact-fact", f"SHARED_PLATFORM={platform}",
                            f"BUILD={scratch / 'build'}",
                            f"NATIVE_BUILD={scratch / 'native'}",
                            f"STAGE={scratch / 'stage'}",
                        ]
                        result = subprocess.run(
                            command, capture_output=True, text=True, timeout=30,
                            check=False,
                        )
                        self.assertEqual(
                            result.returncode, 0, result.stdout + result.stderr,
                        )
                        actual = [line for line in result.stdout.splitlines()
                                  if line.startswith("ARTIFACT=")]
                        self.assertEqual(actual, [
                            "ARTIFACT=" + str(expected_prefix /
                                             f"libwebgpu_doe_c.{suffix}"),
                        ])


if __name__ == "__main__":
    unittest.main()
