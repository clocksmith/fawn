"""Exercise provider selection and exact native manifest identity coverage."""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from bench.tools import generate_tsir_manifest_fixtures as generator


def input_rows() -> list[dict[str, object]]:
    rows = []
    for kernel in sorted(generator.BOOTSTRAP_KERNELS):
        for backend in sorted(generator.NATIVE_BACKENDS):
            rows.append({
                "kernelRef": f"doe.tsir.bootstrap.{kernel}",
                "backend": backend,
                "targetDescriptorCorrectnessHash": "a" * 64,
                "frontendVersion": "test-frontend",
                "tsirSemanticDigest": "b" * 64,
                "tsirRealizationDigest": "c" * 64,
                "emitterDigest": "d" * 64,
                "compilerVersion": "test-compiler",
                "exactnessClass": "bit_exact_solo",
                "algorithmExactInvariants": [],
                "toleranceMetric": "",
                "toleranceEpsilon": 0,
                "rejectionReasons": [],
            })
    return rows


class NativeManifestConsumerTest(unittest.TestCase):
    def test_explicit_native_binary_never_invokes_zig_or_build(self) -> None:
        result = subprocess.CompletedProcess([], 0, json.dumps(input_rows()), "")
        with patch.object(generator.subprocess, "run", return_value=result) as run:
            entries = generator.generate_native_entries(Path("/explicit/c-manifest"))
        self.assertEqual(len(entries), 9)
        self.assertEqual(run.call_count, 1)
        self.assertEqual(run.call_args.args[0], ["/explicit/c-manifest"])
        self.assertEqual(run.call_args.kwargs["timeout"], 30)

    def test_default_native_build_uses_c_owner(self) -> None:
        built = subprocess.CompletedProcess([], 0, "", "")
        result = subprocess.CompletedProcess([], 0, json.dumps(input_rows()), "")
        with patch.object(generator.subprocess, "run", side_effect=[built, result]) as run:
            generator.generate_native_entries(make="chosen-make")
        self.assertEqual(run.call_args_list[0].args[0], [
            "chosen-make", "-C", str(generator.C_COMPILER_DIR),
            "tsir-bootstrap-tools", "-j1",
        ])
        self.assertEqual(run.call_count, 2)

    def test_exact_pair_coverage_not_just_count(self) -> None:
        for mutation in ("duplicate", "wrong-backend", "wrong-kernel", "bare-kernel", "missing"):
            rows = input_rows()
            if mutation == "duplicate":
                rows[-1] = dict(rows[0])
            elif mutation == "wrong-backend":
                rows[-1]["backend"] = "wse3"
            elif mutation == "wrong-kernel":
                rows[-1]["kernelRef"] = "doe.tsir.bootstrap.unexpected"
            elif mutation == "bare-kernel":
                rows[-1]["kernelRef"] = str(rows[-1]["kernelRef"]).removeprefix("doe.tsir.bootstrap.")
            else:
                rows.pop()
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                generator._entries_from_documents(rows, generator.NATIVE_BACKENDS)

    def test_failure_never_falls_back(self) -> None:
        failure = subprocess.CalledProcessError(7, ["c-manifest"])
        with patch.object(generator.subprocess, "run", side_effect=failure) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                generator.generate_native_entries(Path("/explicit/c-manifest"))
        self.assertEqual(run.call_count, 1)

    def test_native_cli_refuses_to_overwrite_historical_default(self) -> None:
        with patch("sys.argv", ["manifest", "--native-only"]):
            with self.assertRaises(SystemExit) as error:
                generator.parse_args()
        self.assertEqual(error.exception.code, 2)

    def test_check_does_not_create_missing_output_directory(self) -> None:
        entries = generator._entries_from_documents(input_rows(), generator.NATIVE_BACKENDS)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "absent"
            with patch("sys.stderr"):
                self.assertEqual(generator.write_entries(output, entries, True), 1)
            self.assertFalse(output.exists())

    def test_native_check_preserves_extra_historical_csl_fixture(self) -> None:
        entries = generator._entries_from_documents(input_rows(), generator.NATIVE_BACKENDS)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            for name, entry in entries.items():
                (output / name).write_text(generator._entry_text(entry), encoding="utf-8")
            retained = output / "gather.wse3.json"
            retained.write_bytes(b"sealed historical bytes\n")
            self.assertEqual(generator.write_entries(output, entries, True), 0)
            self.assertEqual(retained.read_bytes(), b"sealed historical bytes\n")


if __name__ == "__main__":
    unittest.main()
