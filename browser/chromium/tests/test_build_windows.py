"""Exercise real patch admission, Windows batch quoting, and provider closure."""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

OWNER = Path(__file__).resolve().parents[1] / 'scripts/build-windows.py'
spec = importlib.util.spec_from_file_location('build_windows', OWNER)
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class WindowsBuilderTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix='fawn windows ')
        self.root = Path(self.temp.name)
        self.lane = self.root / 'lane'
        (self.lane / 'patches').mkdir(parents=True)
        self.src = self.root / 'src'
        self.roots = (self.src, self.src / 'third_party/dawn')
        self.pins = []
        for root, name in zip(self.roots, ('chromium', 'dawn')):
            root.mkdir(parents=True)
            self.git(root, 'init', '-q')
            self.git(root, 'config', 'user.name', 'Fixture')
            self.git(root, 'config', 'user.email', 'fixture@example.invalid')
            self.git(root, 'config', 'core.autocrlf', 'false')
            (root / 'value').write_text('before\n', encoding='utf-8', newline='\n')
            self.git(root, 'add', 'value')
            self.git(root, 'commit', '-qm', 'Fixture')
            self.pins.append(self.git(root, 'rev-parse', 'HEAD').strip())
            (self.lane / f'patches/{name}-doe-runtime.patch').write_text(
                'diff --git a/value b/value\n--- a/value\n+++ b/value\n@@ -1 +1 @@\n-before\n+after\n',
                encoding='utf-8', newline='\n')

    def tearDown(self) -> None:
        self.temp.cleanup()

    @staticmethod
    def git(root: Path, *args: str) -> str:
        return subprocess.check_output(['git', '-C', str(root), *args], text=True)

    def test_second_revision_mismatch_preserves_first_tree(self) -> None:
        with self.assertRaisesRegex(ValueError, 'source revision mismatch'):
            build.patch_plan(self.src, self.lane, (self.pins[0], '0' * 40))
        self.assertEqual((self.src / 'value').read_text(), 'before\n')

    def test_second_conflict_preserves_first_tree(self) -> None:
        (self.roots[1] / 'value').write_text('unrelated\n')
        with self.assertRaises(subprocess.CalledProcessError):
            build.patch_plan(self.src, self.lane, tuple(self.pins))
        self.assertEqual((self.src / 'value').read_text(), 'before\n')

    def test_applied_pair_is_idempotent(self) -> None:
        pending = build.patch_plan(self.src, self.lane, tuple(self.pins))
        self.assertEqual(len(pending), 2)
        for root, patch in pending:
            self.git(root, 'apply', str(patch))
        self.assertEqual(build.patch_plan(self.src, self.lane, tuple(self.pins)), [])

    def test_full_tracked_diff_binds_changes_outside_patch(self) -> None:
        extra = self.src / 'extra'
        extra.write_text('original\n')
        self.git(self.src, 'add', 'extra')
        self.git(self.src, 'commit', '-qm', 'Extra tracked input')
        evidence = self.root / 'evidence'
        evidence.mkdir()
        before = build.source_diffs(self.src, evidence, 'before')
        extra.write_text('unmanaged change\n')
        after = build.source_diffs(self.src, evidence, 'after')
        self.assertNotEqual(before[0]['sha256'], after[0]['sha256'])
        self.assertIn(b'unmanaged change', (evidence / 'after-chromium.diff').read_bytes())

    def test_provider_dependencies_checked_before_copy(self) -> None:
        native, out = self.root / 'native', self.root / 'out'
        native.mkdir()
        out.mkdir()
        (native / build.RUNTIME_FILES[0]).write_bytes(b'fixture')
        with self.assertRaisesRegex(ValueError, 'missing built provider dependency'):
            build.copy_runtime(native, out)
        self.assertEqual(list(out.iterdir()), [])
        for name in build.RUNTIME_FILES:
            (native / name).write_bytes(name.encode())
        records = build.copy_runtime(native, out)
        self.assertEqual(len(records), len(build.RUNTIME_FILES))
        for name in build.RUNTIME_FILES:
            self.assertEqual((native / name).read_bytes(), (out / name).read_bytes())

    def test_cmd_metacharacters_rejected(self) -> None:
        for bad in ('a&b', '%PATH%', '!bad!', 'a|b', 'a^b', 'a\nb', 'a"b'):
            with self.assertRaises(ValueError):
                build.batch_command(Path('safe.bat'), [bad])

    @unittest.skipUnless(os.name == 'nt', 'real Windows cmd control')
    def test_actual_batch_in_directory_with_spaces(self) -> None:
        script = self.root / 'command fixture.bat'
        script.write_text('@echo off\r\n@echo %~1\r\n', newline='')
        result = subprocess.check_output(build.batch_command(script, ['argument with spaces']), text=True)
        self.assertEqual(result.strip(), 'argument with spaces')


if __name__ == '__main__':
    unittest.main()
