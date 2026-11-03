#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The native object list must not exceed the Windows process argument limit."""
from __future__ import annotations

from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    'windows_native_builder', Path(__file__).resolve().parents[1] / 'native/tools/build_windows.py')
assert SPEC is not None and SPEC.loader is not None
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


class NativeLinkArgumentsTests(unittest.TestCase):
    def test_long_object_list_is_delivered_outside_the_process_command_line(self) -> None:
        objects = ['C:\\long build output\\' + 'x' * 120 + str(i) + '.obj' for i in range(300)]
        self.assertGreater(len(' '.join(objects)), 32767)
        with tempfile.TemporaryDirectory(prefix='native link ') as directory:
            output = Path(directory)

            def compiler_boundary(command: list[str], **kwargs: object) -> None:
                self.assertEqual(command[0], 'clang-cl')
                self.assertEqual(len(command), 2)
                self.assertLess(len(' '.join(command)), 32767)
                response = Path(command[1][1:])
                contents = response.read_text(encoding='utf-8')
                self.assertTrue(contents.startswith('/LD '))
                self.assertIn('"' + objects[0] + '"', contents)
                self.assertIn('"' + objects[-1] + '"', contents)
                self.assertTrue(contents.endswith(' /link kernel32.lib\n'))
                self.assertIs(kwargs['check'], True)

            with patch.object(BUILDER.subprocess, 'run', side_effect=compiler_boundary), redirect_stdout(io.StringIO()):
                BUILDER.invoke_link(['clang-cl', '/LD', *objects, '/link', 'kernel32.lib'], output)

    def test_failed_response_link_is_not_reported_as_success(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            failure = subprocess.CalledProcessError(42, 'clang-cl')
            with patch.object(BUILDER.subprocess, 'run', side_effect=failure), redirect_stdout(io.StringIO()):
                with self.assertRaises(subprocess.CalledProcessError) as caught:
                    BUILDER.invoke_link(['clang-cl', '/LD', 'missing.obj'], Path(directory))
            self.assertEqual(caught.exception.returncode, 42)

    def test_fixture_link_preserves_the_native_library_recipe(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            with patch.object(BUILDER.subprocess, 'run'), redirect_stdout(io.StringIO()):
                BUILDER.invoke_link(['clang-cl', '/LD', 'native.obj'], output)
                original = (output / 'native-link.rsp').read_bytes()
                BUILDER.invoke_link(['clang-cl', 'fault.obj'], output, 'signal-fault-link.rsp')
            self.assertEqual((output / 'native-link.rsp').read_bytes(), original)
            self.assertNotEqual((output / 'signal-fault-link.rsp').read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
