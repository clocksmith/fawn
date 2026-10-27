"""Independent retained-C-package byte/source rejection controls; no GPU work."""

from __future__ import annotations

import copy
import hashlib
import unittest

from bench.lib.c_package_identity import c_manifest_failures


class CPackageIdentityTests(unittest.TestCase):
    def fixture(self) -> tuple[dict, dict, dict, dict]:
        runtime = {'libwebgpu_doe_c.so': b'C native provider', 'libdoe_compiler.so': b'C compiler'}
        sources = {'runtime/c/native/src/procedures.c': b'local C resolver'}
        sha = lambda value: hashlib.sha256(value).hexdigest()
        manifest = {'source_commit': 'a' * 40, 'source_tree': 'b' * 40,
                    'source_inputs': {name: sha(value) for name, value in sources.items()},
                    'artifacts': {'lib/' + name: sha(value) for name, value in runtime.items()}}
        provider = {'libraryFlavor': 'doe-c', 'buildMetadataSource': 'c-package',
                    'buildIdentity': {'sourceCommit': manifest['source_commit'], 'sourceTree': manifest['source_tree'],
                                      'artifactSha256': sha(runtime['libwebgpu_doe_c.so'])}}
        return manifest, provider, runtime, sources

    def verify(self, manifest: dict, provider: dict, runtime: dict, sources: dict) -> list[str]:
        return c_manifest_failures(manifest, provider, 'libwebgpu_doe_c.so', runtime.get, sources.get,
                                   lambda commit: 'b' * 40 if commit == 'a' * 40 else None)

    def test_exact_retained_C_contents_and_sources_pass(self) -> None:
        self.assertEqual(self.verify(*self.fixture()), [])

    def test_changed_native_or_compiler_or_committed_source_reject(self) -> None:
        for member in ['libwebgpu_doe_c.so', 'libdoe_compiler.so']:
            manifest, provider, runtime, sources = self.fixture()
            runtime[member] += b'changed'
            self.assertTrue(self.verify(manifest, provider, runtime, sources), member)
        manifest, provider, runtime, sources = self.fixture()
        sources['runtime/c/native/src/procedures.c'] += b'changed'
        self.assertTrue(self.verify(manifest, provider, runtime, sources))

    def test_missing_compiler_or_source_and_forged_identity_reject(self) -> None:
        for field in ['source_inputs', 'artifacts', 'source_tree', 'source_commit']:
            manifest, provider, runtime, sources = self.fixture()
            del manifest[field]
            self.assertTrue(self.verify(manifest, provider, runtime, sources), field)
        for identity in [None, {}, {'sourceCommit': 'wrong'}, {'artifactSha256': '0' * 64}]:
            manifest, provider, runtime, sources = self.fixture()
            provider['buildIdentity'] = identity
            self.assertTrue(self.verify(manifest, provider, runtime, sources))

    def test_zig_metadata_label_or_path_traversal_cannot_authorize_C(self) -> None:
        manifest, provider, runtime, sources = self.fixture()
        provider['buildMetadataSource'] = 'prebuild'
        self.assertTrue(self.verify(manifest, provider, runtime, sources))
        manifest, provider, runtime, sources = self.fixture()
        manifest['artifacts']['lib/../libdoe_compiler.so'] = manifest['artifacts'].pop('lib/libdoe_compiler.so')
        self.assertTrue(self.verify(manifest, provider, runtime, sources))
        manifest, provider, runtime, sources = self.fixture()
        manifest['source_inputs']['../outside'] = 'c' * 64
        self.assertTrue(self.verify(manifest, provider, runtime, sources))


if __name__ == '__main__':
    unittest.main()
