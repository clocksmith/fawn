#!/usr/bin/env python3
"""Author generator/build checks. Independent evaluator cases are not imported."""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

OWNER = Path(__file__).resolve().parents[1]
ROOT = OWNER.parents[2]


def load_tool(name: str) -> object:
    spec = importlib.util.spec_from_file_location(name, OWNER / "tools" / f"{name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


generator = load_tool("generate_config")
verifier = load_tool("verify_archive")


def run(command: list[str], *, success: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, timeout=30,
                            env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
    if success and result.returncode != 0:
        raise AssertionError(f"command failed: {command}\n{result.stdout}{result.stderr}")
    if not success and result.returncode == 0:
        raise AssertionError(f"command unexpectedly succeeded: {command}")
    return result


class TestJsonAdmission(unittest.TestCase):
    def test_malformed_grammar_is_rejected(self) -> None:
        cases = [b"", b" ", b"\xef\xbb\xbf{}", b"\v{}", b"{} x", b"[] true",
                 b"[1,]", b'{"a":1,}', b"[,1]", b'{:"x"}', b'{"a" 1}',
                 b"{a:1}", b"[1 2]", b"[", b'{"a":', b'"unfinished',
                 b'"bad\\x20"', b'"bad\n"', b"+1", b"01", b"-.1", b"1.",
                 b"1e", b"1e+", b"NaN", b"Infinity", b"-Infinity", b"TRUE",
                 b"falsex", b"nul", b"[}", b'{"x":]}' ]
        for raw in cases:
            with self.subTest(raw=raw), self.assertRaises(generator.JsonSyntaxError):
                generator.parse_json(raw)

    def test_valid_grammar_preserves_number_tokens(self) -> None:
        root, duplicates = generator.parse_json(
            b' {"n":[-0,0,1,-10,0.25,1E+7,-2e-9,true,false,null]}\r\n')
        self.assertFalse(duplicates)
        values = root.pairs[0][1]
        self.assertEqual([item.raw for item in values[:7]],
                         ["-0", "0", "1", "-10", "0.25", "1E+7", "-2e-9"])
        self.assertIs(values[7], True)
        self.assertIs(values[8], False)
        self.assertIsNone(values[9])

    def test_utf8_scalars_surrogate_pairs_and_nul_have_exact_bytes(self) -> None:
        root, _ = generator.parse_json(b'["\xc3\xa9","\\uD83D\\uDE80","x\\u0000y"]')
        self.assertEqual([value.encode("utf-8") for value in root],
                         [b"\xc3\xa9", b"\xf0\x9f\x9a\x80", b"x\x00y"])
        for raw in (b'"\xff"', b'"\xc0\x80"', b'"\xed\xa0\x80"',
                    b'"\\uD800"', b'"\\uDC00"', b'"\\uD800x"', b'"\x00"'):
            with self.subTest(raw=raw), self.assertRaises(generator.JsonSyntaxError):
                generator.parse_json(raw)

    def test_deep_unknown_data_does_not_use_python_recursion(self) -> None:
        nested = b"[" * 12000 + b"null" + b"]" * 12000
        raw = b'{"defaultMode":"doe_metal_ownership","unknown":' + nested + b"}"
        self.assertEqual(generator.behavior_config(raw).default_mode,
                         "DOE_DROPIN_MODE_METAL")
        raw = b'{"schemaVersion":2,"symbols":[],"unknown":' + nested + b"}"
        self.assertEqual(generator.ownership_config(raw), ())

    def test_giant_numbers_are_valid_unknown_values(self) -> None:
        for token in (b"9" * 10000, b"1e" + b"9" * 10000):
            with self.subTest(token_length=len(token)):
                raw = b'{"unknown":' + token + b',"defaultMode":"mixed_ownership"}'
                self.assertEqual(generator.behavior_config(raw).default_mode,
                                 "DOE_DROPIN_MODE_MIXED")
                raw = (b'{"schemaVersion":3,"symbols":[{"symbol":"ok",'
                       b'"owner":"shared","unused":' + token + b"}]}")
                self.assertEqual(generator.ownership_config(raw),
                                 ((b"ok", "DOE_DROPIN_OWNER_SHARED"),))

    def test_behavior_duplicate_escaped_or_unused_key_invalidates_document(self) -> None:
        for extra in (b'"x":0,"x":1', b'"x":0,"\\u0078":1',
                      b'"unused":[{"nested":1,"nested":2}]'):
            raw = b'{"defaultMode":"doe_vulkan_ownership",' + extra + b"}"
            self.assertEqual(generator.behavior_config(raw), generator.Behavior())

    def test_behavior_strict_type_and_fatal_precedence(self) -> None:
        cases = [
            (b'{"defaultMode":"bad","strictFallbackForbidden":0}', "DOE_DROPIN_OK", 1),
            (b'{"defaultMode":"bad","strictFallbackForbidden":null}', "DOE_DROPIN_OK", 1),
            (b'{"defaultMode":"bad","laneModes":{"unused":"bad"}}',
             "DOE_DROPIN_FATAL_DEFAULT_MODE", 1),
            (b'{"defaultMode":false,"laneModes":{"unused":"bad"}}',
             "DOE_DROPIN_FATAL_LANE_MODE", 1),
            (b'{"strictFallbackForbidden":false,"laneModes":{"unused":"bad"}}',
             "DOE_DROPIN_OK", 0),
        ]
        for raw, status, strict in cases:
            with self.subTest(raw=raw):
                behavior = generator.behavior_config(raw)
                self.assertEqual((behavior.status, behavior.strict), (status, strict))
                if status != "DOE_DROPIN_OK":
                    self.assertEqual((behavior.observe_environment, behavior.lane_modes), (0, ()))

    def test_lane_keys_keep_order_case_nul_and_nonstring_default(self) -> None:
        raw = (b'{"defaultMode":"DOE_METAL_OWNERSHIP","strictFallbackForbidden":false,'
               b'"laneModes":{"A":"MIXED_OWNERSHIP","a":null,"x\\u0000y":"bad"}}')
        behavior = generator.behavior_config(raw)
        self.assertEqual(behavior.default_mode, "DOE_DROPIN_MODE_METAL")
        self.assertEqual(behavior.observe_environment, 1)
        self.assertEqual(behavior.lane_modes,
                         ((b"A", "DOE_DROPIN_MODE_MIXED"),
                          (b"a", "DOE_DROPIN_MODE_USE_DEFAULT"),
                          (b"x\x00y", "DOE_DROPIN_MODE_USE_DEFAULT")))
        self.assertEqual(generator.behavior_config(b'{"laneModes":{}}').observe_environment, 1)
        self.assertEqual(generator.behavior_config(b'{"laneModes":[]}').observe_environment, 0)

    def test_ascii_token_parsing_does_not_trim_fold_unicode_or_truncate(self) -> None:
        for value in (" mixed_ownership", "mixed_ownership\u0000", "mıxed_ownership"):
            raw = json.dumps({"defaultMode": value}).encode()
            self.assertEqual(generator.behavior_config(raw).status,
                             "DOE_DROPIN_FATAL_DEFAULT_MODE")
        for value in (" shared", "shared\u0000", "ſhared"):
            raw = json.dumps({"schemaVersion": 3, "symbols": [
                {"symbol": "s", "owner": value}]}).encode()
            self.assertEqual(generator.ownership_config(raw), ())

    def test_ownership_profile_rejects_only_specified_coercions(self) -> None:
        for version in (b'"2"', b"2.0", b"2e0", b"true", b"-0", b"4"):
            raw = b'{"schemaVersion":' + version + b',"symbols":[{"symbol":"s","owner":"shared"}]}'
            self.assertEqual(generator.ownership_config(raw), ())
        for symbol, owner in ((b"[115]", b'"shared"'), (b'"s"', b"[115,104,97,114,101,100]")):
            raw = b'{"schemaVersion":3,"symbols":[{"symbol":' + symbol + b',"owner":' + owner + b"}]}"
            self.assertEqual(generator.ownership_config(raw), ())
        for version in (b"2", b"3"):
            raw = b'{"schemaVersion":' + version + b',"symbols":[{"symbol":"","owner":"SHARED","tier":false}]}'
            self.assertEqual(generator.ownership_config(raw), ((b"", "DOE_DROPIN_OWNER_SHARED"),))

    def test_ownership_known_duplicates_reject_but_unknown_duplicates_admit(self) -> None:
        accepted = (b'{"schemaVersion":3,"unknown":1,"unknown":2,"symbols":['
                    b'{"symbol":"a","owner":"shared","x":{"a":1,"a":2},"x":0},'
                    b'{"symbol":"a","owner":"dawn_delegate"}]}')
        self.assertEqual(generator.ownership_config(accepted),
                         ((b"a", "DOE_DROPIN_OWNER_SHARED"),
                          (b"a", "DOE_DROPIN_OWNER_DAWN")))
        for raw in (b'{"schemaVersion":3,"schemaVersion":3,"symbols":[]}',
                    b'{"schemaVersion":3,"symbols":[],"symbols":[]}',
                    b'{"schemaVersion":3,"symbols":[{"symbol":"a","\\u0073ymbol":"b","owner":"shared"}]}',
                    b'{"schemaVersion":3,"symbols":[{"symbol":"a","owner":"shared","owner":"shared"}]}'):
            self.assertEqual(generator.ownership_config(raw), ())

    def test_invalid_later_ownership_row_rejects_entire_prefix(self) -> None:
        raw = (b'{"schemaVersion":3,"symbols":[{"symbol":"valid","owner":"shared"},'
               b'{"symbol":"bad","owner":"unknown"}]}')
        self.assertEqual(generator.ownership_config(raw), ())


class TestPublication(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="doe-config-author-", dir="/tmp")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.behavior = self.directory / "behavior.json"
        self.ownership = self.directory / "ownership.json"
        self.output = self.directory / "generated.c"
        self.behavior.write_bytes(b"{}")
        self.ownership.write_bytes(b"{}")

    def command(self) -> list[str]:
        return [sys.executable, str(OWNER / "tools/generate_config.py"),
                "--behavior", str(self.behavior), "--ownership", str(self.ownership),
                "--output", str(self.output)]

    def test_exact_input_boundary_and_empty_input(self) -> None:
        for raw in (b"", b"{}" + b" " * 65534):
            self.behavior.write_bytes(raw)
            run(self.command())
            self.assertTrue(self.output.is_file())
        old = self.output.read_bytes()
        self.behavior.write_bytes(b" " * 65537)
        result = run(self.command(), success=False)
        self.assertIn("exceeds 65536-byte", result.stderr)
        self.assertEqual(self.output.read_bytes(), old)

    def test_input_reader_never_requests_unbounded_bytes(self) -> None:
        stream = mock.MagicMock()
        stream.__enter__.return_value = stream
        stream.read.return_value = b"{}"
        path = mock.Mock()
        path.open.return_value = stream
        self.assertEqual(generator.read_input(path), b"{}")
        stream.read.assert_called_once_with(65537)

    def test_missing_and_read_error_preserve_previous_output(self) -> None:
        run(self.command())
        old = self.output.read_bytes()
        self.ownership.unlink()
        run(self.command(), success=False)
        self.assertEqual(self.output.read_bytes(), old)
        self.ownership.mkdir()
        run(self.command(), success=False)
        self.assertEqual(self.output.read_bytes(), old)

    def test_replace_failure_and_memory_failure_preserve_previous_output(self) -> None:
        self.output.write_bytes(b"previous complete bytes")
        with mock.patch.object(generator.os, "replace", side_effect=OSError("injected replace failure")):
            with self.assertRaises(OSError):
                generator.atomic_write(self.output, b"new")
        self.assertEqual(self.output.read_bytes(), b"previous complete bytes")
        self.assertEqual(sorted(path.name for path in self.directory.iterdir()),
                         ["behavior.json", "generated.c", "ownership.json"])
        args = argparse.Namespace(behavior=self.behavior, ownership=self.ownership,
                                  output=self.output, profile=1)
        with mock.patch.object(generator, "parse_args", return_value=args), \
                mock.patch.object(generator, "render_config", side_effect=MemoryError("injected")):
            self.assertEqual(generator.main(), 1)
        self.assertEqual(self.output.read_bytes(), b"previous complete bytes")

    def test_deterministic_emission_and_noop_publication(self) -> None:
        self.behavior.write_bytes(b'{"laneModes":{"x":null}}')
        self.ownership.write_bytes(b'{"schemaVersion":2,"symbols":[{"symbol":"x\\u0000\\u00e9","owner":"shared"}]}')
        run(self.command())
        old, stamp = self.output.read_bytes(), self.output.stat().st_mtime_ns
        run(self.command())
        self.assertEqual(self.output.read_bytes(), old)
        self.assertEqual(self.output.stat().st_mtime_ns, stamp)
        self.assertEqual(old, generator.render_config(self.behavior.read_bytes(), self.ownership.read_bytes()))

    def test_compiled_data_has_exact_lengths_and_single_configuration(self) -> None:
        self.behavior.write_bytes(b'{"defaultMode":"doe_d3d12_ownership","laneModes":{"x\\u0000y":"mixed_ownership"}}')
        self.ownership.write_bytes(b'{"schemaVersion":3,"symbols":[{"symbol":"\\u00e9\\uD83D\\uDE80\\u0000","owner":"DOE_VULKAN"},{"symbol":"","owner":"shared"}]}')
        run(self.command())
        probe = self.directory / "probe.c"
        probe.write_text('''#include "doe_dropin_policy_private.h"
#include <assert.h>
#include <string.h>
int main(void) {
    const DoeDropinEmbeddedConfig *c = &doe_dropin_policy_generated_config;
    const uint8_t wanted[] = {195,169,240,159,154,128,0};
    assert(c->admission_profile == 1 && c->behavior_status == DOE_DROPIN_OK);
    assert(c->default_mode == DOE_DROPIN_MODE_D3D12 && c->strict_no_fallback == 1);
    assert(c->lane_mode_count == 1 && c->lane_modes[0].key.length == 3);
    assert(c->lane_modes[0].key.data[1] == 0 && c->lane_modes[0].mode == DOE_DROPIN_MODE_MIXED);
    assert(c->ownership_count == 2 && c->ownership[0].symbol.length == sizeof(wanted));
    assert(memcmp(c->ownership[0].symbol.data, wanted, sizeof(wanted)) == 0);
    assert(c->ownership[0].owner == DOE_DROPIN_OWNER_VULKAN);
    assert(c->ownership[1].symbol.length == 0 && c->ownership[1].symbol.data == NULL);
    return 0;
}
''', encoding="utf-8")
        output = self.directory / "probe"
        run(["gcc", *verifier.FLAGS, "-I", str(OWNER / "include"),
             str(probe), str(self.output), "-o", str(output)])
        run([str(output)])


class TestBuildFreshness(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="doe-build-author-", dir="/tmp")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.root = self.directory / "repo"
        for name in verifier.REQUIRED_FILES:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, target)
        self.owner = self.root / verifier.OWNER
        self.build = self.directory / "build"
        self.archive = self.build / "libdoe_dropin_policy.a"
        self.receipt = Path(str(self.archive) + ".receipt.json")
        self.make()

    def make(self, *, success: bool = True) -> subprocess.CompletedProcess[str]:
        return run(["make", "--no-print-directory", "-C", str(self.owner),
                    f"BUILD={self.build}", "all"], success=success)

    def verify(self, *extra: str, success: bool = True) -> subprocess.CompletedProcess[str]:
        return run([sys.executable, str(self.owner / "tools/verify_archive.py"),
                    "--repo-root", str(self.root), "--archive", str(self.archive),
                    "--target", "x86_64-linux-gnu", "--profile", "1", *extra], success=success)

    def test_noop_preserves_objects_receipt_and_archive(self) -> None:
        paths = [self.archive, self.receipt, self.build / "policy.o",
                 self.build / "generated_config.o", self.build / "generated_config.c"]
        before = [(path.read_bytes(), path.stat().st_mtime_ns) for path in paths]
        self.make()
        self.verify()
        self.assertEqual(before, [(path.read_bytes(), path.stat().st_mtime_ns) for path in paths])

    def test_each_closed_input_invalidates_even_with_preserved_mtime(self) -> None:
        names = [*verifier.CONFIG_FILES,
                 str(verifier.OWNER / "include/doe_dropin_policy.h"),
                 str(verifier.OWNER / "include/doe_dropin_policy_private.h"),
                 str(verifier.OWNER / "src/policy.c"),
                 str(verifier.OWNER / "tools/generate_config.py"),
                 str(verifier.OWNER / "tools/verify_archive.py"),
                 str(verifier.OWNER / "Makefile")]
        for name in names:
            with self.subTest(path=name):
                path = self.root / name
                timestamp = path.stat()
                old_receipt = self.receipt.read_bytes()
                old_object_time = (self.build / "policy.o").stat().st_mtime_ns
                path.write_bytes(path.read_bytes() + b"\n")
                os.utime(path, ns=(timestamp.st_atime_ns, timestamp.st_mtime_ns))
                result = self.verify(success=False)
                self.assertIn(name, result.stderr)
                self.make()
                self.verify()
                self.assertNotEqual(self.receipt.read_bytes(), old_receipt)
                self.assertNotEqual((self.build / "policy.o").stat().st_mtime_ns, old_object_time)

    def test_json_change_regenerates_actual_configuration(self) -> None:
        generated = self.build / "generated_config.c"
        old = generated.read_bytes()
        path = self.root / "config/dropin-abi-behavior.json"
        path.write_bytes(b'{"defaultMode":"doe_vulkan_ownership"}')
        self.verify(success=False)
        self.make()
        self.assertNotEqual(generated.read_bytes(), old)
        self.verify()

    def test_generation_failure_cannot_reuse_successful_stale_build(self) -> None:
        generated = self.build / "generated_config.c"
        old_generated, old_archive = generated.read_bytes(), self.archive.read_bytes()
        path = self.root / "config/dropin-abi-behavior.json"
        path.write_bytes(b" " * 65537)
        self.make(success=False)
        self.assertEqual(generated.read_bytes(), old_generated)
        self.assertEqual(self.archive.read_bytes(), old_archive)
        self.verify(success=False)
        path.write_bytes(b"{}")
        self.make()
        self.verify()

    def test_missing_archive_receipt_wrong_target_profile_and_tamper_fail(self) -> None:
        self.verify("--target", "aarch64-linux-gnu", success=False)
        self.verify("--profile", "2", success=False)
        old = self.receipt.read_bytes()
        self.receipt.unlink()
        self.verify(success=False)
        self.receipt.write_bytes(old)
        archive = self.archive.read_bytes()
        self.archive.unlink()
        self.verify(success=False)
        self.archive.write_bytes(archive + b"x")
        self.assertIn("SHA-256", self.verify(success=False).stderr)
        self.archive.write_bytes(archive)
        record = json.loads(old)
        record["settings"]["mandatory_c_flags"].remove("-fPIC")
        self.receipt.write_text(json.dumps(record), encoding="utf-8")
        self.assertIn("mandatory_c_flags", self.verify(success=False).stderr)

    def test_generator_execution_failure_preserves_previous_outputs(self) -> None:
        generated = self.build / "generated_config.c"
        old_generated, old_archive = generated.read_bytes(), self.archive.read_bytes()
        path = self.owner / "tools/generate_config.py"
        source = path.read_text(encoding="utf-8")
        path.write_text(source.replace("raise SystemExit(main())", "raise SystemExit(19)"),
                        encoding="utf-8")
        self.make(success=False)
        self.assertEqual(generated.read_bytes(), old_generated)
        self.assertEqual(self.archive.read_bytes(), old_archive)
        self.verify(success=False)
        path.write_text(source, encoding="utf-8")
        self.make()
        self.verify()

    def test_changed_generated_output_is_rejected_then_regenerated(self) -> None:
        generated = self.build / "generated_config.c"
        old = generated.read_bytes()
        generated.write_bytes(old + b"/* changed build output */\n")
        self.assertIn("generated C SHA-256 mismatch", self.verify(success=False).stderr)
        self.make()
        self.assertEqual(generated.read_bytes(), old)
        self.verify()

    def test_forced_rebuild_has_identical_archive_and_receipt_bytes(self) -> None:
        old_archive, old_receipt = self.archive.read_bytes(), self.receipt.read_bytes()
        run(["make", "--no-print-directory", "-B", "-C", str(self.owner),
             f"BUILD={self.build}", "all"])
        self.assertEqual(self.archive.read_bytes(), old_archive)
        self.assertEqual(self.receipt.read_bytes(), old_receipt)
        self.verify()

    def test_record_cannot_bless_stale_object_after_recapturing_settings(self) -> None:
        source = self.root / verifier.OWNER / "src/policy.c"
        source.write_bytes(source.read_bytes() + b"\n")
        settings = self.build / "build_settings.json"
        command = [sys.executable, str(self.owner / "tools/verify_archive.py"),
                   "--repo-root", str(self.root)]
        run([*command, "--write-build-settings", str(settings)])
        result = run([*command, "--record", "--archive", str(self.archive),
                      "--settings", str(settings)], success=False)
        self.assertIn("stale object receipt", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
