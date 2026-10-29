#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host source-contract checks, not a Windows Dawn producer acceptance claim."""
import importlib.util
import json
from pathlib import Path
import sys
import unittest
import os
import hashlib
import tempfile
import shutil
import subprocess
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_windows_dawn import DAWN_REVISION, abi_probe, artifact_query, stage_artifacts, stage_sdk_fxc, deterministic_git_checkout, verify_header_bytes  # noqa: E402


class ProducerContract(unittest.TestCase):
    def test_pinned_probe_is_complete_and_reproducible(self):
        repo = ROOT.parents[2]
        policy = json.loads((repo / 'config/webgpu-abi-source.json').read_text())['source']
        header = (repo / policy['path']).read_text()
        probe = abi_probe(header)
        self.assertEqual(probe, abi_probe(header))
        self.assertEqual(probe.count('printf('), 989)
        self.assertIn('offsetof(WGPUComputePassDescriptor, timestampWrites)', probe)
        self.assertIn('WGPUFeatureName_ShaderF16=', probe)
        self.assertEqual(len(DAWN_REVISION), 40)

    def test_checkout_config_is_process_scoped_and_preserves_inherited_options(self):
        with patch.dict(os.environ, {'GIT_CONFIG_COUNT': '1', 'GIT_CONFIG_KEY_0': 'test.key',
                                     'GIT_CONFIG_VALUE_0': 'retained'}, clear=True):
            deterministic_git_checkout()
            self.assertEqual(os.environ['GIT_CONFIG_COUNT'], '3')
            self.assertEqual(os.environ['GIT_CONFIG_VALUE_0'], 'retained')
            self.assertEqual(os.environ['GIT_CONFIG_KEY_1'], 'core.autocrlf')
            self.assertEqual(os.environ['GIT_CONFIG_VALUE_1'], 'false')
            self.assertEqual(os.environ['GIT_CONFIG_KEY_2'], 'core.eol')
            self.assertEqual(os.environ['GIT_CONFIG_VALUE_2'], 'lf')

    def test_header_bytes_are_never_normalized_to_pass(self):
        blob = b'header\n'
        expected = hashlib.sha256(blob).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / 'webgpu.h'
            with patch('build_windows_dawn.subprocess.run', return_value=SimpleNamespace(stdout=blob)), \
                 patch('build_windows_dawn.capture', return_value='a' * 40):
                header.write_bytes(blob)
                verify_header_bytes(header, expected, root)
                header.write_bytes(b'header\r\n')
                with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                    verify_header_bytes(header, expected, root)
                evidence = json.loads((root / 'header-integrity.json').read_text())
                self.assertEqual(evidence['checkout']['crlf'], 1)
                self.assertEqual(evidence['gitBlob']['crlf'], 0)
                self.assertEqual(evidence['gitBlob']['sha256'], expected)

    def test_target_artifacts_override_products_and_are_hash_bound(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build, products = root / 'build', root / 'products'
            build.mkdir()
            products.mkdir()
            dll, imports = build / 'webgpu_dawn.dll', products / 'webgpu_dawn.lib'
            dll.write_bytes(b'actual target dll')
            imports.write_bytes(b'actual target import library')
            (build / 'unrelated.dll').write_bytes(b'never select this')
            manifest = build / 'doe-dawn-artifacts.txt'
            manifest.write_text(f'{dll}\n{imports}\n')
            evidence = stage_artifacts(build, products)
            self.assertEqual((products / dll.name).read_bytes(), dll.read_bytes())
            self.assertFalse((products / 'unrelated.dll').exists())
            self.assertEqual(evidence[dll.name]['target_path'], 'build/webgpu_dawn.dll')
            self.assertEqual(evidence[dll.name]['sha256'], hashlib.sha256(dll.read_bytes()).hexdigest())
            for invalid in [f'{dll}\n', f'{dll}\n{imports}\nextra\n',
                            f'webgpu_dawn.dll\n{imports}\n',
                            f'{root / dll.name}\n{imports}\n',
                            f'{build / "unrelated.dll"}\n{imports}\n',
                            f'{dll}\n{build / imports.name}\n']:
                manifest.write_text(invalid)
                with self.assertRaises(ValueError):
                    stage_artifacts(build, products)

    def test_fxc_uses_selected_sdk_and_preserves_explicit_dependency_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sdk, products = root / 'sdk', root / 'products'
            products.mkdir()
            source = sdk / 'bin/10.0.28000.0/x64/d3dcompiler_47.dll'
            source.parent.mkdir(parents=True)
            source.write_bytes(b'selected SDK x64 FXC')
            headers, exports = '8664 machine (x64)', 'D3DCompile\nD3DDisassemble'
            with patch('build_windows_dawn.capture', side_effect=[headers, exports]):
                evidence = stage_sdk_fxc(products, sdk, '10.0.28000.0\\')
            self.assertEqual((products / source.name).read_bytes(), source.read_bytes())
            self.assertEqual(evidence, {'source_path': str(source), 'windows_sdk_version': '10.0.28000.0',
                                       'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                                       'architecture': 'x64',
                                       'headers_sha256': hashlib.sha256(headers.encode()).hexdigest(),
                                       'exports_sha256': hashlib.sha256(exports.encode()).hexdigest()})
            with self.assertRaisesRegex(ValueError, 'already exists'):
                stage_sdk_fxc(products, sdk, '10.0.28000.0')

    def test_missing_fxc_never_searches_other_sdk_or_system_directories(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            products = root / 'products'
            products.mkdir()
            for sdk, version in [(root / 'sdk', ''), (Path('relative'), '10.0.28000.0'),
                                 (root / 'sdk', '../10.0.28000.0')]:
                with self.assertRaisesRegex(ValueError, 'selected Windows SDK root and version'):
                    stage_sdk_fxc(products, sdk, version)
            with self.assertRaisesRegex(ValueError, 'FXC runtime missing'):
                stage_sdk_fxc(products, root / 'sdk', '10.0.28000.0')
            self.assertEqual(list(products.iterdir()), [])

    def test_wrong_architecture_or_compiler_exports_do_not_stage_fxc(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sdk, products = root / 'sdk', root / 'products'
            products.mkdir()
            source = sdk / 'bin/10.0.28000.0/x64/d3dcompiler_47.dll'
            source.parent.mkdir(parents=True)
            source.write_bytes(b'wrong dependency')
            for headers, exports, error in [
                    ('14C machine (x86)', 'D3DCompile\nD3DDisassemble', 'must be x64'),
                    ('8664 machine (x64)', 'D3DCompile', 'required compiler exports')]:
                with patch('build_windows_dawn.capture', side_effect=[headers, exports]):
                    with self.assertRaisesRegex(ValueError, error):
                        stage_sdk_fxc(products, sdk, '10.0.28000.0')
                self.assertEqual(list(products.iterdir()), [])

    @unittest.skipUnless(shutil.which('cmake'), 'requires host CMake for generated target-path fixture')
    def test_cmake_query_tracks_target_override(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, build = root / 'source', root / 'build'
            source.mkdir()
            query = root / 'query.cmake'
            artifact_query(query)
            # Imported target avoids compiling or claiming a Windows binary on this host.
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.24)\nproject(ArtifactControl NONE)\n'
                'set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")\n'
                'add_library(webgpu_dawn SHARED IMPORTED)\n'
                'set_target_properties(webgpu_dawn PROPERTIES\n'
                ' IMPORTED_LOCATION "${PROJECT_BINARY_DIR}/override/webgpu_dawn.dll"\n'
                ' IMPORTED_IMPLIB "${PROJECT_BINARY_DIR}/imports/webgpu_dawn.lib")\n')
            subprocess.run(['cmake', '-S', str(source), '-B', str(build),
                            '-DCMAKE_SYSTEM_NAME=Windows',
                            '-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=' + str(query)],
                           check=True, capture_output=True, timeout=30)
            self.assertEqual((build / 'doe-dawn-artifacts.txt').read_text().splitlines(),
                             [str(build / 'override/webgpu_dawn.dll'),
                              str(build / 'imports/webgpu_dawn.lib')])

    def test_incomplete_probe_fails_closed(self):
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            abi_probe('typedef struct WGPUExample { int field; } WGPUExample;')

    def test_windows_scripts_import_without_execution(self):
        for name in ['build_windows.py', 'build_windows_dawn.py']:
            spec = importlib.util.spec_from_file_location('check_' + name, ROOT / 'tools' / name)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            self.assertTrue(callable(module.main))


if __name__ == '__main__':
    unittest.main()
