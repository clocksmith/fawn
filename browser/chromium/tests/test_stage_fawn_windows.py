#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host-only producer admission controls; fixture bytes are not browser images."""
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/stage-fawn-windows.py'
SPEC = importlib.util.spec_from_file_location('stage_windows', SCRIPT)
stage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stage)


class ProducerAdmission(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.browser, self.native, self.launcher = root / 'browser', root / 'native', root / 'launcher'
        for path in (self.browser, self.native / 'bin', self.launcher):
            path.mkdir(parents=True)
        self.library_names = ('webgpu_doe_c.dll', 'doe_compiler.dll', 'dxcompiler.dll', 'dxil.dll')
        self.browser_record = {'completed': True, 'browserArtifacts': [],
                               'selectedToolAndSdkInputs': [], 'selectedToolAndSdkInputsAfter': []}
        for name in ('chrome.exe', 'chrome.dll'):
            file = self.browser / name
            file.write_bytes(('explicit host fixture: ' + name).encode())
            self.browser_record['browserArtifacts'].append({'path': str(file), 'sha256': stage.digest(file)})
        (self.browser / 'args.gn').write_bytes(b'explicit host fixture')
        self.browser_record['argsSha256'] = stage.digest(self.browser / 'args.gn')
        self.native_record = {'artifacts': {}}
        for name in self.library_names:
            path = self.native / 'bin' / name
            path.write_bytes(('explicit host fixture: ' + name).encode())
            self.native_record['artifacts']['bin/' + name] = stage.digest(path)
        (self.launcher / 'Fawn.exe').write_bytes(b'explicit host fixture')
        self.launcher_record = {'completed': True, 'privateRuntimeRelativeDirectory': 'lib/doe',
                                'launcherSha256': stage.digest(self.launcher / 'Fawn.exe'),
                                'inputs': {str(stage.LANE / 'launcher/fawn_windows.c'):
                                           stage.digest(stage.LANE / 'launcher/fawn_windows.c')}}

    def admit(self) -> tuple:
        for path, data in ((self.browser / 'fawn-windows-browser-build.json', self.browser_record),
                           (self.native / 'MANIFEST.json', self.native_record),
                           (self.launcher / 'launcher-build.json', self.launcher_record)):
            path.write_text(json.dumps(data))
        return stage.validate_producers(self.browser, self.native, self.launcher / 'Fawn.exe')

    def test_exact_producer_bytes_are_admitted(self) -> None:
        self.assertEqual(self.admit(), (self.browser_record, self.native_record, self.launcher_record))

    def test_incomplete_browser_and_missing_implementation_are_rejected(self) -> None:
        self.browser_record['completed'] = False
        with self.assertRaisesRegex(ValueError, 'must be complete'):
            self.admit()
        self.browser_record['completed'] = True
        self.browser_record['browserArtifacts'] = self.browser_record['browserArtifacts'][:1]
        with self.assertRaisesRegex(ValueError, 'bind both'):
            self.admit()

    def test_changed_c_compiler_and_missing_dxc_are_rejected(self) -> None:
        compiler = self.native / 'bin/doe_compiler.dll'
        original = compiler.read_bytes()
        compiler.write_bytes(b'changed compiler fixture')
        with self.assertRaisesRegex(ValueError, 'changed input'):
            self.admit()
        compiler.write_bytes(original)
        (self.native / 'bin/dxcompiler.dll').unlink()
        with self.assertRaisesRegex(ValueError, 'changed input'):
            self.admit()

    def test_old_root_runtime_launcher_is_rejected(self) -> None:
        self.launcher_record['privateRuntimeRelativeDirectory'] = '.'
        with self.assertRaisesRegex(ValueError, 'private lib/doe'):
            self.admit()

    def test_different_launcher_source_is_rejected(self) -> None:
        self.launcher_record['inputs'] = {'fawn_windows.c': hashlib.sha256(b'old source fixture').hexdigest()}
        with self.assertRaisesRegex(ValueError, 'source differs'):
            self.admit()


class ActionArgumentDiagnostics(unittest.TestCase):
    def test_real_failed_query_retains_both_raw_streams_and_exit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            command = [sys.executable, '-c',
                       'import sys;sys.stdout.write("partial query");sys.stderr.write("SDK rejection");sys.exit(17)']
            with self.assertRaises(subprocess.CalledProcessError) as caught:
                stage.upstream_action_arguments(command, root, dict(os.environ), root)
            self.assertEqual(caught.exception.returncode, 17)
            self.assertEqual((root / 'upstream-action-args.json').read_bytes(), b'partial query')
            self.assertEqual((root / 'upstream-action-args.stderr').read_bytes(), b'SDK rejection')

    def test_real_query_timeout_retains_partial_streams(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            command = [sys.executable, '-c',
                       'import sys,time;print("partial query",flush=True);print("still waiting",file=sys.stderr,flush=True);time.sleep(30)']
            with self.assertRaises(subprocess.TimeoutExpired):
                stage.upstream_action_arguments(command, root, dict(os.environ), root, timeout=2)
            self.assertEqual((root / 'upstream-action-args.json').read_bytes().splitlines(), [b'partial query'])
            self.assertEqual((root / 'upstream-action-args.stderr').read_bytes().splitlines(), [b'still waiting'])


if __name__ == '__main__':
    unittest.main()
