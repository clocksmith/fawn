#!/usr/bin/env python3
"""Generate TSIR bootstrap manifest lowering fixture entries."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from bench.tools.tsir_manifest_lowering import (
    ManifestLoweringInputs,
    build_manifest_lowering_entry,
    manifest_lowering_entry_digest,
)


DEFAULT_OUTPUT_DIR = REPO_ROOT / "bench" / "fixtures" / "tsir-manifest-entries"
ZIG_WORKDIR = REPO_ROOT / "runtime" / "zig"
ZIG_INPUT_BINARY = (
    ZIG_WORKDIR / "zig-out" / "bin" / "doe-tsir-bootstrap-manifest-inputs"
)
EXPECTED_BOOTSTRAP_FIXTURE_COUNT = 12
NATIVE_BACKENDS = frozenset({"webgpu-generic", "msl", "spir-v"})
BOOTSTRAP_KERNELS = frozenset({"fused_gemv", "rms_norm", "gather"})
C_COMPILER_DIR = REPO_ROOT / "runtime" / "c" / "compiler"
C_INPUT_BINARY = C_COMPILER_DIR / "build" / "doe-tsir-bootstrap-manifest-inputs-c"


def _run_zig_input_tool(zig: str) -> list[dict[str, Any]]:
    subprocess.run(
        [
            zig,
            "build",
            "tsir-bootstrap-manifest-inputs",
            "-Denable-csl=true",
        ],
        cwd=ZIG_WORKDIR,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    result = subprocess.run(
        [str(ZIG_INPUT_BINARY)],
        cwd=REPO_ROOT,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    docs = json.loads(result.stdout)
    if not isinstance(docs, list):
        raise ValueError("bootstrap manifest input tool must emit a JSON array")
    return docs


def _lowering_inputs(doc: dict[str, Any]) -> ManifestLoweringInputs:
    return ManifestLoweringInputs(
        kernel_ref=doc["kernelRef"],
        backend=doc["backend"],
        target_descriptor_correctness_hash=doc[
            "targetDescriptorCorrectnessHash"
        ],
        frontend_version=doc["frontendVersion"],
        tsir_semantic_digest=doc["tsirSemanticDigest"],
        tsir_realization_digest=doc["tsirRealizationDigest"],
        emitter_digest=doc["emitterDigest"],
        compiler_version=doc["compilerVersion"],
        exactness_class=doc["exactnessClass"],
        algorithm_exact_invariants=tuple(doc["algorithmExactInvariants"]),
        tolerance_metric=doc["toleranceMetric"],
        tolerance_epsilon=doc["toleranceEpsilon"],
        rejection_reasons=tuple(doc["rejectionReasons"]),
    )


def _fixture_name(entry: dict[str, Any]) -> str:
    kernel = entry["kernelRef"].removeprefix("doe.tsir.bootstrap.")
    return f"{kernel}.{entry['backend']}.json"


def _entries_from_documents(
    documents: list[dict[str, Any]], backends: frozenset[str]
) -> dict[str, dict[str, Any]]:
    fixtures: dict[str, dict[str, Any]] = {}
    for doc in documents:
        if not isinstance(doc, dict):
            raise ValueError("bootstrap manifest input rows must be JSON objects")
        kernel_ref = doc.get("kernelRef")
        if kernel_ref not in {
            f"doe.tsir.bootstrap.{kernel}" for kernel in BOOTSTRAP_KERNELS
        }:
            raise ValueError(f"unexpected bootstrap kernel identity: {kernel_ref!r}")
        entry = build_manifest_lowering_entry(_lowering_inputs(doc))
        name = _fixture_name(entry)
        if name in fixtures:
            raise ValueError(f"duplicate TSIR manifest fixture name: {name}")
        fixtures[name] = entry
    expected_names = {
        f"{kernel}.{backend}.json"
        for kernel in BOOTSTRAP_KERNELS for backend in backends
    }
    if set(fixtures) != expected_names:
        raise ValueError(
            "bootstrap fixture identity set differs: "
            f"missing={sorted(expected_names - set(fixtures))}; "
            f"unexpected={sorted(set(fixtures) - expected_names)}"
        )
    return fixtures


def generate_entries(zig: str) -> dict[str, dict[str, Any]]:
    """Preserve the historical twelve-row, explicitly CSL-inclusive route."""
    return _entries_from_documents(
        _run_zig_input_tool(zig), NATIVE_BACKENDS | {"wse3"}
    )


def generate_native_entries(
    binary: Path | None = None, make: str = "make"
) -> dict[str, dict[str, Any]]:
    """Run the C source consumer without building or invoking any Zig tool.

    The default binary is rebuilt through its canonical owner. An explicitly
    supplied binary is consumed as-is, with no fallback to another provider.
    Retained emitter source bytes remain provenance inputs, not executable code.
    """
    if binary is None:
        subprocess.run(
            [make, "-C", str(C_COMPILER_DIR), "tsir-bootstrap-tools", "-j1"],
            cwd=REPO_ROOT, check=True, capture_output=True, text=True,
            timeout=300,
        )
        binary = C_INPUT_BINARY
    result = subprocess.run(
        [str(binary.resolve())], cwd=REPO_ROOT, check=True,
        capture_output=True, text=True, timeout=30,
    )
    documents = json.loads(result.stdout)
    if not isinstance(documents, list):
        raise ValueError("C bootstrap input tool must emit a JSON array")
    return _entries_from_documents(documents, NATIVE_BACKENDS)


def _entry_text(entry: dict[str, Any]) -> str:
    return json.dumps(entry, indent=2, sort_keys=True) + "\n"


def write_entries(
    output_dir: Path,
    fixtures: dict[str, dict[str, Any]],
    check: bool,
) -> int:
    if not check:
        output_dir.mkdir(parents=True, exist_ok=True)
    failures: list[str] = []
    for name, entry in sorted(fixtures.items()):
        path = output_dir / name
        expected = _entry_text(entry)
        if check:
            if not path.exists():
                failures.append(f"missing fixture: {path}")
                continue
            actual = path.read_text(encoding="utf-8")
            if actual != expected:
                failures.append(f"stale fixture: {path}")
            continue
        path.write_text(expected, encoding="utf-8")
        digest = manifest_lowering_entry_digest(entry)
        print(f"{path} manifestLoweringEntryDigest={digest}")
    if failures:
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="Directory where fixture JSON files are written.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Fail if committed fixtures differ from regenerated entries.",
    )
    parser.add_argument(
        "--zig",
        default=os.environ.get("ZIG", "zig"),
        help="Zig executable used to run the bootstrap input tool.",
    )
    parser.add_argument(
        "--native-only", action="store_true",
        help="Use the C compiler for the nine non-CSL rows; no Zig executable.",
    )
    parser.add_argument(
        "--c-manifest-binary", type=Path,
        help="Explicit prebuilt C manifest tool (native-only); never falls back.",
    )
    parser.add_argument(
        "--make", default="make",
        help="Make executable for the default native-only C tool build.",
    )
    args = parser.parse_args()
    if args.c_manifest_binary is not None and not args.native_only:
        parser.error("--c-manifest-binary requires --native-only")
    if (args.native_only and not args.check and
            args.output_dir.resolve() == DEFAULT_OUTPUT_DIR.resolve()):
        parser.error(
            "native-only generation needs a separate --output-dir; "
            "use --check to compare the existing historical fixtures"
        )
    return args


def main() -> int:
    try:
        args = parse_args()
        fixtures = (
            generate_native_entries(args.c_manifest_binary, args.make)
            if args.native_only else generate_entries(args.zig)
        )
        return write_entries(args.output_dir, fixtures, args.check)
    except (
        KeyError,
        ValueError,
        json.JSONDecodeError,
        subprocess.CalledProcessError,
        subprocess.TimeoutExpired,
        OSError,
    ) as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
