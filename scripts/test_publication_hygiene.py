"""Focused Git-history controls for the private-home-path publication lint."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCANNER = Path(__file__).with_name("check-publication-hygiene.py").resolve()
PRIVATE_PATH = "/" + "home/private-developer/project"


class PublicationHygieneTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.git("init", "-q")
        self.git("config", "user.name", "Test")
        self.git("config", "user.email", "test@example.invalid")
        self.write("history.txt", PRIVATE_PATH + "\nunchanged\n")
        self.commit()
        self.base = self.git("rev-parse", "HEAD").strip()

    def git(self, *args: str) -> str:
        return subprocess.check_output(
            ["git", *args], cwd=self.root, text=True, stderr=subprocess.PIPE,
        )

    def write(self, name: str, content: str) -> None:
        (self.root / name).write_text(content)

    def commit(self) -> None:
        self.git("add", ".")
        self.git("commit", "-qm", "fixture")

    def scan(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(SCANNER), *args], cwd=self.root,
            text=True, capture_output=True, check=False,
        )

    def test_added_private_line_is_rejected(self) -> None:
        self.write("new.txt", "clean\n" + PRIVATE_PATH + "\n")
        self.commit()
        result = self.scan("--base-ref", self.base)
        self.assertEqual(result.returncode, 1)
        self.assertIn("new.txt:2: Linux user home", result.stdout)

    def test_removed_private_line_is_ignored(self) -> None:
        self.write("history.txt", "unchanged\n")
        self.commit()
        self.assertEqual(self.scan("--base-ref", self.base).returncode, 0)

    def test_unchanged_historical_line_and_clean_change_pass(self) -> None:
        self.write("history.txt", PRIVATE_PATH + "\nchanged clean line\n")
        self.commit()
        self.assertEqual(self.scan("--base-ref", self.base).returncode, 0)
        self.assertEqual(self.scan().returncode, 1)

    def test_missing_empty_and_zero_base_fail_closed(self) -> None:
        for base in ("nonexistent-base", "", "0" * 40):
            with self.subTest(base=base):
                result = self.scan("--base-ref", base)
                self.assertEqual(result.returncode, 1)
                self.assertIn("could not inspect Git history", result.stderr)

    def test_added_path_in_existing_file_is_rejected(self) -> None:
        self.write("history.txt", PRIVATE_PATH + "\nunchanged\n" + PRIVATE_PATH + "\n")
        self.commit()
        result = self.scan("--base-ref", self.base)
        self.assertEqual(result.returncode, 1)
        self.assertIn("history.txt:3: Linux user home", result.stdout)
        self.assertIn("1 private path(s)", result.stdout)

    def test_clean_new_file_passes(self) -> None:
        self.write("new.txt", "ordinary content\n")
        self.commit()
        self.assertEqual(self.scan("--base-ref", self.base).returncode, 0)


if __name__ == "__main__":
    unittest.main()
