"""Source-bound optional CSL controls; not a Zig compiler execution claim."""
import json
import unittest
import jsonschema

from check_test_reachability import quarantined_csl_source
from generate_test_suites import INVENTORY_PATH, ROOT, _validate, generate


class CslQuarantineTests(unittest.TestCase):
    def setUp(self):
        self.inventory = json.loads(INVENTORY_PATH.read_text())

    def test_inventory_preserves_explicit_csl_suite_and_native_roots(self):
        self.assertEqual(_validate(self.inventory), [])
        suites = self.inventory['suites']
        native = {entry['path'] for entry in suites['wgsl']['entries']}
        optional = {entry['path'] for entry in suites['csl']['entries']}
        self.assertEqual(suites['csl']['output'], 'test_suite_csl.zig')
        self.assertIn('tests/wgsl/translation_csl_facade_test.zig', optional)
        self.assertIn('tests/wgsl/translation_facade_test.zig', native)
        for path in native:
            self.assertNotIn('/emit/csl/', path)
            self.assertNotIn('emit_csl_', path)
            self.assertNotIn('mod_cslc_', path)
        for name in ['tsir_pipeline', 'tsir_emit_backend_skeleton', 'tsir_emit_kernel_body']:
            path = f'tests/wgsl/{name}_test.zig'
            self.assertIn(path, native)
            self.assertIn(path, optional)
        for suite in suites.values():
            self.assertNotIn('csl', suite.get('includes', []))

    def test_quarantine_manifest_is_valid_and_preserves_sources(self):
        repository = ROOT.parents[1]
        manifest = json.loads((repository / 'config/csl-quarantine.json').read_text())
        schema = json.loads((repository / 'config/csl-quarantine.schema.json').read_text())
        jsonschema.Draft202012Validator(schema).validate(manifest)
        paths = [record['path'] for record in manifest['preservedSourceBlobs']]
        self.assertEqual(len(paths), len(set(paths)))
        self.assertTrue(paths)
        for path in paths:
            self.assertTrue((repository / path).is_file(), path)
            self.assertTrue(quarantined_csl_source(path.removeprefix('runtime/zig/')))
        build = (ROOT / 'build.zig').read_text()
        self.assertIn('"enable-csl", "Explicitly enable quarantined CSL compiler and tools") orelse false', build)
        for step in manifest['buildSteps']:
            identifier = step.replace('-', '_') + '_step'
            self.assertNotIn('b.getInstallStep().dependOn(' + identifier + ')', build)
            self.assertIn('if (enable_csl) ' + identifier + '.dependOn(', build)

    def test_generated_roots_are_current(self):
        for path, content in generate(self.inventory).items():
            self.assertEqual(path.read_text(), content, str(path))

    def test_reachability_excludes_only_csl_owners(self):
        for path in ['src/compiler/wgsl/emit/csl/emit_csl.zig',
                     'src/compiler/tsir/emit_csl.zig',
                     'src/spatial/csl/csl_sim_runner.zig']:
            self.assertTrue(quarantined_csl_source(path))
        for path in ['src/compiler/tsir/planner.zig',
                     'src/compiler/tsir/emit_kernel_body.zig',
                     'src/compiler/wgsl/frontend/sema.zig',
                     'src/backend/vulkan/mod.zig']:
            self.assertFalse(quarantined_csl_source(path))

    def test_facades_gate_actual_csl_imports(self):
        for relative in ['src/compiler/wgsl/mod.zig', 'src/compiler/tsir/mod.zig']:
            source = (ROOT / relative).read_text()
            self.assertIn('@import("build_options").enable_csl', source)
            for line in source.splitlines():
                if '@import(' in line and ('emit_csl' in line or 'translate_csl' in line):
                    self.assertIn('if (enable_csl)', line)
        self.assertIn('if (@import("build_options").enable_csl) struct',
                      (ROOT / 'src/compiler/mod.zig').read_text())


if __name__ == '__main__':
    unittest.main()
