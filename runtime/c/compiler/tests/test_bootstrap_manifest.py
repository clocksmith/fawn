# SPDX-License-Identifier: MIT
"""Exercise real C bootstrap manifest lowering and retained Zig equivalence."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import unittest

from bench.tools.tsir_manifest_lowering import (
    ManifestLoweringInputs,
    build_manifest_lowering_entry,
)


ROOT = Path(__file__).resolve().parents[4]
C_BINARY = Path(os.environ.get(
    "DOE_TSIR_MANIFEST_CLI",
    ROOT / "runtime/c/compiler/build/doe-tsir-bootstrap-manifest-inputs-c",
)).resolve()
ZIG_BINARY = Path(os.environ.get(
    "DOE_TSIR_MANIFEST_ZIG",
    ROOT / "runtime/zig/zig-out/bin/doe-tsir-bootstrap-manifest-inputs",
)).resolve()
KERNEL_NAMES = ("fused_gemv", "rms_norm", "gather")


def run_tool(binary: Path, root: Path) -> subprocess.CompletedProcess[bytes]:
    """Bound both elapsed execution and the complete child process group."""
    process = subprocess.Popen(
        [str(binary)], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        start_new_session=True,
    )
    try:
        stdout, stderr = process.communicate(timeout=30)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.communicate()
        raise
    return subprocess.CompletedProcess([str(binary)], process.returncode,
                                       stdout, stderr)


class BootstrapManifestTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory(prefix="fawn-manifest-")
        self.root = Path(self.directory.name)
        # These are the tool's explicit input sets; never recursively scan or
        # copy benchmark/results trees into a fixture.
        source_dir = self.root / "runtime/zig/src/compiler/tsir"
        source_dir.mkdir(parents=True)
        for name in ("emit_webgpu.zig", "emit_msl.zig", "emit_spir_v.zig",
                     "emit_text_skeleton.zig", "emit_kernel_body.zig",
                     "emit_semantic_metadata.zig"):
            shutil.copyfile(ROOT / "runtime/zig/src/compiler/tsir" / name,
                            source_dir / name)
        kernel_dir = self.root / "runtime/zig/tests/tsir/bootstrap"
        kernel_dir.mkdir(parents=True)
        for name in KERNEL_NAMES:
            shutil.copyfile(ROOT / "runtime/zig/tests/tsir/bootstrap" / f"{name}.wgsl",
                            kernel_dir / f"{name}.wgsl")

    def tearDown(self) -> None:
        self.directory.cleanup()

    def c_manifest(self) -> bytes:
        result = run_tool(C_BINARY, self.root)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        entries = json.loads(result.stdout)
        self.assertEqual(len(entries), 9)
        self.assertEqual([entry["backend"] for entry in entries],
                         ["webgpu-generic", "msl", "spir-v"] * 3)
        for entry in entries:
            lowered = build_manifest_lowering_entry(ManifestLoweringInputs(
                kernel_ref=entry["kernelRef"],
                backend=entry["backend"],
                target_descriptor_correctness_hash=entry[
                    "targetDescriptorCorrectnessHash"],
                frontend_version=entry["frontendVersion"],
                tsir_semantic_digest=entry["tsirSemanticDigest"],
                tsir_realization_digest=entry["tsirRealizationDigest"],
                emitter_digest=entry["emitterDigest"],
                compiler_version=entry["compilerVersion"],
                exactness_class=entry["exactnessClass"],
                algorithm_exact_invariants=tuple(entry["algorithmExactInvariants"]),
                tolerance_metric=entry["toleranceMetric"],
                tolerance_epsilon=entry["toleranceEpsilon"],
                rejection_reasons=tuple(entry["rejectionReasons"]),
            ))
            self.assertEqual(lowered["kernelRef"], entry["kernelRef"])
            self.assertEqual(entry["rejectionReasons"], [])
            for key in ("emitterDigest", "targetDescriptorCorrectnessHash",
                        "tsirRealizationDigest", "tsirSemanticDigest"):
                self.assertRegex(entry[key], r"^[0-9a-f]{64}$")
                self.assertNotEqual(entry[key], "0" * 64)
        return result.stdout

    def compare_zig(self, actual: bytes) -> None:
        if not ZIG_BINARY.is_file():
            self.skipTest("retained Zig executable unavailable")
        result = run_tool(ZIG_BINARY, self.root)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        self.assertEqual(actual, result.stdout)

    def test_source_pipeline_and_zig_differential(self) -> None:
        self.compare_zig(self.c_manifest())

    def test_source_bytes_and_semantics_change_identity(self) -> None:
        baseline = json.loads(self.c_manifest())
        path = self.root / "runtime/zig/tests/tsir/bootstrap/gather.wgsl"
        source = path.read_text()
        # Parameter/axis names flow through typed IR, rather than choosing a
        # hand-written shape based on the fixed filename or kernel label.
        path.write_text(source.replace("let t: u32", "let token: u32")
                        .replace("t >=", "token >=")
                        .replace("indices[t]", "indices[token]")
                        .replace("t *", "token *"))
        changed = self.c_manifest()
        entries = json.loads(changed)
        self.assertNotEqual(baseline[6]["tsirSemanticDigest"],
                            entries[6]["tsirSemanticDigest"])
        self.assertEqual(baseline[:6], entries[:6])
        self.compare_zig(changed)

    def test_module_globals_are_lowered_through_typed_owner(self) -> None:
        path = self.root / "runtime/zig/tests/tsir/bootstrap/gather.wgsl"
        original = path.read_text()
        cases = [
            ("const zero: u32 = 0u;\n", "let h: u32 = gid.x + zero;"),
            ("alias Index = u32;\nconst zero: Index = 0u;\n", "let h: u32 = gid.x + zero;"),
            ("var<private> scratch: u32;\n", "scratch = gid.x;\n    let h: u32 = scratch;"),
            ("var<workgroup> scratch: array<u32, 64>;\n", "scratch[gid.x] = gid.x;\n    let h: u32 = scratch[gid.x];"),
            ("override zero: u32 = 0u;\n", "let h: u32 = gid.x + zero;"),
        ]
        for declarations, statement in cases:
            with self.subTest(declarations=declarations):
                path.write_text(declarations + original.replace("let h: u32 = gid.x;", statement))
                self.compare_zig(self.c_manifest())

    def test_wrong_typed_input_is_rejected(self) -> None:
        path = self.root / "runtime/zig/tests/tsir/bootstrap/gather.wgsl"
        path.write_text(path.read_text().replace("let row: u32 = indices[t];",
                                                "let row: u32 = false;"))
        result = run_tool(C_BINARY, self.root)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")
        self.assertIn(b"WGSL analysis", result.stderr)

    def test_missing_source_and_missing_emitter_fail_without_publication(self) -> None:
        for path in ("runtime/zig/tests/tsir/bootstrap/gather.wgsl",
                     "runtime/zig/src/compiler/tsir/emit_webgpu.zig"):
            target = self.root / path
            saved = target.read_bytes()
            target.unlink()
            result = run_tool(C_BINARY, self.root)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b"")
            self.assertIn(b"TSIR bootstrap manifest", result.stderr)
            target.write_bytes(saved)

    def test_input_cap(self) -> None:
        path = self.root / "runtime/zig/tests/tsir/bootstrap/gather.wgsl"
        path.write_bytes(b" " * ((1 << 20) + 1))
        result = run_tool(C_BINARY, self.root)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")

    def test_emitter_identity_comes_from_actual_reference_source(self) -> None:
        baseline = json.loads(self.c_manifest())
        path = self.root / "runtime/zig/src/compiler/tsir/emit_webgpu.zig"
        path.write_bytes(path.read_bytes() + b"\n// identity mutation\n")
        entries = json.loads(self.c_manifest())
        self.assertNotEqual(baseline[0]["emitterDigest"], entries[0]["emitterDigest"])
        self.assertEqual(baseline[0]["tsirSemanticDigest"],
                         entries[0]["tsirSemanticDigest"])
        self.assertNotEqual(baseline[0]["tsirRealizationDigest"],
                            entries[0]["tsirRealizationDigest"])
        self.assertEqual(baseline[1:3], entries[1:3])


if __name__ == "__main__":
    unittest.main()
