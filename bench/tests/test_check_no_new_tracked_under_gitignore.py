from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOL = REPO_ROOT / "bench/tools/check_no_new_tracked_under_gitignore.py"


class CheckNoNewTrackedUnderGitignoreTest(unittest.TestCase):
    """Exercise Git's actual ignore, index and revision-range behavior."""

    def _run(self, repo: Path, paths: list[str]) -> subprocess.CompletedProcess[str]:
        proc = subprocess.run(
            [sys.executable, str(TOOL), *paths],
            cwd=repo,
            capture_output=True,
            check=False,
        )
        return subprocess.CompletedProcess(
            proc.args, proc.returncode,
            proc.stdout.decode("utf-8"), proc.stderr.decode("utf-8"),
        )

    def _init_repo(self, repo: Path, gitignore_body: str) -> None:
        subprocess.run(
            ["git", "init", "-q", "-b", "main", str(repo)], check=True
        )
        (repo / ".gitignore").write_text(gitignore_body, encoding="utf-8")
        # Both staged and revision-range checks require a real baseline.
        subprocess.run(
            ["git", "-C", str(repo), "add", ".gitignore"], check=True
        )
        subprocess.run(
            [
                "git", "-C", str(repo),
                "-c", "user.email=test@example.com",
                "-c", "user.name=test",
                "commit", "-q", "-m", "initial",
            ],
            check=True,
        )

    def test_returns_zero_when_path_not_ignored(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            self._init_repo(repo, "bench/out/\n")
            (repo / "src").mkdir()
            (repo / "src/foo.py").write_text("# foo\n", encoding="utf-8")
            r = self._run(repo, ["src/foo.py"])
            self.assertEqual(r.returncode, 0, r.stderr)

    def test_returns_one_when_path_matches_gitignore(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            self._init_repo(repo, "bench/out/\n")
            (repo / "bench/out").mkdir(parents=True)
            (repo / "bench/out/leak.json").write_text("{}\n", encoding="utf-8")
            r = self._run(repo, ["bench/out/leak.json"])
            self.assertEqual(r.returncode, 1)
            self.assertIn("bench/out/leak.json", r.stderr)
            self.assertIn(".gitignore", r.stderr)

    def test_returns_zero_when_invoked_with_no_args_and_nothing_staged(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            self._init_repo(repo, "bench/out/\n")
            r = self._run(repo, [])
            self.assertEqual(r.returncode, 0, r.stderr)

    def test_mixed_args_fails_only_on_ignored_path(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            self._init_repo(repo, "bench/out/\n")
            (repo / "src").mkdir()
            (repo / "src/foo.py").write_text("# foo\n", encoding="utf-8")
            (repo / "bench/out").mkdir(parents=True)
            (repo / "bench/out/leak.json").write_text("{}\n", encoding="utf-8")
            r = self._run(repo, ["src/foo.py", "bench/out/leak.json"])
            self.assertEqual(r.returncode, 1)
            # Only the ignored path should appear in the error list, not
            # the clean one.
            self.assertIn("bench/out/leak.json", r.stderr)
            self.assertNotIn("src/foo.py", r.stderr)


    def test_negated_ignore_rule_admits_required_fixture(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            self._init_repo(repo, "generated/*\n!generated/fixture.json\n")
            r = self._run(repo, ["generated/noise.json", "generated/fixture.json"])
            self.assertEqual(r.returncode, 1, r.stderr)
            self.assertIn("generated/noise.json", r.stderr)
            self.assertNotIn("generated/fixture.json", r.stderr)
            self.assertEqual(self._run(repo, ["generated/fixture.json"]).returncode, 0)

    def test_staged_force_add_preserves_unusual_filename(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            self._init_repo(repo, "*.bin\n")
            name = "output with space\tand newline\r\n.bin"
            (repo / name).write_bytes(b"fixture")
            subprocess.run(
                ["git", "-C", str(repo), "add", "-f", "--", name], check=True,
            )
            r = self._run(repo, [])
            self.assertEqual(r.returncode, 1, r.stderr)
            self.assertIn(name, r.stderr)

    def test_revision_range_rejects_committed_output_with_clean_index(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            self._init_repo(repo, "*.bin\n")
            (repo / "output.bin").write_bytes(b"fixture")
            subprocess.run(
                ["git", "-C", str(repo), "add", "-f", "output.bin"], check=True,
            )
            subprocess.run([
                "git", "-C", str(repo), "-c", "user.email=test@example.com",
                "-c", "user.name=test", "commit", "-qm", "output",
            ], check=True)
            self.assertEqual(self._run(repo, []).returncode, 0)
            r = self._run(repo, ["--base-ref", "HEAD~1"])
            self.assertEqual(r.returncode, 1, r.stderr)
            self.assertIn("output.bin", r.stderr)

    def test_rename_into_ignored_path_is_an_addition(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            self._init_repo(repo, "*.bin\n")
            (repo / "source.txt").write_text("fixture\n", encoding="utf-8")
            subprocess.run(["git", "-C", str(repo), "add", "source.txt"], check=True)
            subprocess.run([
                "git", "-C", str(repo), "-c", "user.email=test@example.com",
                "-c", "user.name=test", "commit", "-qm", "source",
            ], check=True)
            subprocess.run(
                ["git", "-C", str(repo), "mv", "source.txt", "output.bin"], check=True,
            )
            r = self._run(repo, [])
            self.assertEqual(r.returncode, 1, r.stderr)
            self.assertIn("output.bin", r.stderr)

    def test_invalid_revision_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            self._init_repo(repo, "*.bin\n")
            r = self._run(repo, ["--base-ref", "missing-revision"])
            self.assertEqual(r.returncode, 1, r.stderr)
            self.assertIn("ignore-policy check failed", r.stderr)


if __name__ == "__main__":
    unittest.main()
