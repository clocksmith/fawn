"""Compiled ABI controls: matching byte count alone cannot prove public layout."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import tempfile
import unittest

from bench.tools import check_chromium_c_abi as abi


HEADER = '''
typedef enum WGPUKind {WGPUKind_One = 1} WGPUKind;
typedef struct WGPUExample {unsigned first; unsigned second;} WGPUExample;
void wgpuFixture(WGPUExample value);
'''


class ChromiumCAbiTests(unittest.TestCase):
    def fixture(self, root: Path, target: str) -> argparse.Namespace:
        include = root / 'include'
        include.mkdir()
        base = root / 'webgpu.h'
        base.write_text(HEADER)
        dawn = root / 'pinned.h'
        dawn.write_text(target)
        selector = root / 'selector.patch'
        selector.write_text('+if (!load(&mDawnProcs.fixture, "wgpuFixture")) {\n')
        return argparse.Namespace(out=root / 'result', compiler='clang', compiler_arg=[], timeout=10,
                                  base_header=base, header=dawn, selector=selector, c_include=include)

    @unittest.skipUnless(shutil.which('clang'), 'real CPU Clang/linker required')
    def test_independent_compiler_layout_and_declarations_pass(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            report = abi.run(self.fixture(Path(scratch), HEADER))
            self.assertEqual(report['status'], 'pass')
            self.assertEqual(report['inputDrift'], [])
            self.assertTrue(report['procedures'][0]['same'])
            self.assertEqual(report['structures']['c']['WGPUExample']['second.offset'], 4)
            self.assertTrue((Path(scratch) / 'result/c-ast.stdout').is_file())

    @unittest.skipUnless(shutil.which('clang'), 'real CPU Clang/linker required')
    def test_same_size_but_reordered_fields_reject(self) -> None:
        target = HEADER.replace('unsigned first; unsigned second;', 'unsigned second; unsigned first;')
        with tempfile.TemporaryDirectory() as scratch:
            report = abi.run(self.fixture(Path(scratch), target))
            self.assertEqual(report['status'], 'blocked')
            self.assertEqual(report['structures']['c']['WGPUExample']['sizeof'],
                             report['structures']['dawn']['WGPUExample']['sizeof'])
            self.assertEqual(len(report['layoutFailures']), 1)

    @unittest.skipUnless(shutil.which('clang'), 'real CPU Clang/linker required')
    def test_signature_and_enum_mismatch_cannot_hide_behind_layout(self) -> None:
        target = HEADER.replace('void wgpuFixture', 'int wgpuFixture').replace('WGPUKind_One = 1', 'WGPUKind_One = 2')
        with tempfile.TemporaryDirectory() as scratch:
            report = abi.run(self.fixture(Path(scratch), target))
            self.assertEqual(report['layoutFailures'], [])
            self.assertFalse(report['procedures'][0]['same'])
            self.assertEqual(report['constantFailures'][0]['constant'], 'WGPUKind_One')
            self.assertEqual(report['status'], 'blocked')

    def test_trailing_count_and_alias_are_preserved_and_nested_struct_is_not_silently_omitted(self) -> None:
        found = abi.structures('typedef struct WGPUExample {void *pointer; unsigned count;} WGPUExample; '
                               'typedef WGPUExample WGPUAlias;')
        self.assertEqual(found['WGPUAlias'], ['pointer', 'count'])
        with self.assertRaisesRegex(ValueError, 'nested'):
            abi.structures('typedef struct WGPUExample {struct {unsigned x;} nested;} WGPUExample;')
        self.assertEqual(abi.compare_constants({'DOE_SHARED_FENCE_MTL_EVENT': 5},
                                              {'WGPUSharedFenceType_MTLSharedEvent': 5}), [])
        self.assertTrue(abi.compare_constants({'DOE_SHARED_FENCE_MTL_EVENT': 4},
                                             {'WGPUSharedFenceType_MTLSharedEvent': 5}))

    @unittest.skipUnless(shutil.which('clang'), 'real CPU Clang/linker required')
    def test_forward_declaration_cannot_hide_a_required_descriptor_layout(self) -> None:
        dummy = 'typedef struct WGPUDummy {int x;} WGPUDummy;'
        with tempfile.TemporaryDirectory() as scratch:
            args = self.fixture(Path(scratch), HEADER + dummy)
            args.base_header.write_text(HEADER.replace(
                'typedef struct WGPUExample {unsigned first; unsigned second;} WGPUExample;',
                'typedef struct WGPUExample WGPUExample;') + dummy)
            report = abi.run(args)
            self.assertTrue(report['procedures'][0]['same'])
            self.assertEqual(report['status'], 'blocked')
            self.assertTrue(any(row['struct'] == 'WGPUExample' for row in report['layoutFailures']))


if __name__ == '__main__':
    unittest.main()
