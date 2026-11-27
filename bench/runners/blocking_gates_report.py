"""Checks that consume one comparison report, shared by full and focused runs."""

from __future__ import annotations

import argparse
import shutil
import sys
from collections.abc import Callable
from pathlib import Path

GATES = Path(__file__).resolve().parents[1] / "gates"
GateRunner = Callable[[str, list[str]], None]


def add_report_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--report",
        default="bench/out/dawn-vs-doe.json",
        help="Comparison report produced by the compare lane.",
    )
    parser.add_argument(
        "--gates",
        default="config/gates.json",
        help="Gate policy config path passed to check_correctness.py",
    )
    parser.add_argument(
        "--quirk",
        default="examples/quirks/intel_gen12_temp_buffer.json",
        help="Reference quirk path passed to check_correctness.py",
    )
    parser.add_argument(
        "--with-backend-selection-gate",
        action="store_true",
        help="Run backend_selection_gate.py after trace gate.",
    )
    parser.add_argument(
        "--backend-runtime-policy",
        default="config/backend-runtime-policy.json",
        help="Backend runtime policy path passed to backend_selection_gate.py.",
    )
    parser.add_argument(
        "--backend-selection-lane",
        default="",
        help="Optional lane override passed to backend_selection_gate.py.",
    )
    parser.add_argument(
        "--with-shader-artifact-gate",
        action="store_true",
        help="Run shader_artifact_gate.py after trace gate.",
    )
    parser.add_argument(
        "--shader-artifact-schema",
        default="config/shader-artifact.schema.json",
        help="Shader artifact schema path passed to shader_artifact_gate.py.",
    )
    parser.add_argument(
        "--shader-artifact-require-manifest",
        action="store_true",
        help="Pass --require-manifest to shader_artifact_gate.py.",
    )
    parser.add_argument(
        "--shader-artifact-spirv-val",
        default="",
        help="Optional spirv-val executable passed to shader_artifact_gate.py.",
    )
    parser.add_argument(
        "--shader-artifact-require-spirv-validation",
        action="store_true",
        help="Fail shader artifact gate when SPIR-V artifacts are present but not validated.",
    )
    parser.add_argument(
        "--with-metal-sync-conformance-gate",
        action="store_true",
        help="Run metal_sync_conformance.py after trace gate.",
    )
    parser.add_argument(
        "--backend-timing-policy",
        default="config/backend-timing-policy.json",
        help="Backend timing policy path passed to sync/timing gates.",
    )
    parser.add_argument(
        "--with-metal-timing-policy-gate",
        action="store_true",
        help="Run metal_timing_policy_gate.py after trace gate.",
    )
    parser.add_argument(
        "--with-vulkan-sync-conformance-gate",
        action="store_true",
        help="Run vulkan_sync_conformance.py after trace gate.",
    )
    parser.add_argument(
        "--with-vulkan-timing-policy-gate",
        action="store_true",
        help="Run vulkan_timing_policy_gate.py after trace gate.",
    )
    parser.add_argument(
        "--with-comparable-runtime-invariants-gate",
        action="store_true",
        help="Run comparable_runtime_invariants_gate.py after trace gate.",
    )
    parser.add_argument(
        "--with-comparability-coherence-gate",
        action=argparse.BooleanOptionalAction,
        default=True,
        help=(
            "Run comparability_coherence_gate.py after trace gate. Default: enabled. "
            "Pass --no-with-comparability-coherence-gate only for diagnostic report "
            "audits that are not claim evidence."
        ),
    )
    parser.add_argument(
        "--comparability-coherence-benchmark-policy",
        default="config/benchmark-methodology-thresholds.json",
        help="Benchmark policy path passed to comparability_coherence_gate.py.",
    )
    parser.add_argument(
        "--with-compare-output-partition-gate",
        action=argparse.BooleanOptionalAction,
        default=True,
        help=(
            "Run compare_output_partition_gate.py after trace gate. Default: enabled. "
            "Pass --no-with-compare-output-partition-gate only for non-claim "
            "diagnostic audits that intentionally violate claim/diagnostic partitioning."
        ),
    )
    parser.add_argument(
        "--with-structural-equivalence-gate",
        action=argparse.BooleanOptionalAction,
        default=True,
        help=(
            "Run structural_equivalence_gate.py after trace gate. Default: enabled. "
            "Pass --no-with-structural-equivalence-gate to opt out for diagnostic-only "
            "runs that legitimately fail structural parity (e.g. workloads exercising "
            "Doe-only coverage where Dawn reports unsupported)."
        ),
    )
    parser.add_argument(
        "--trace-semantic-parity-mode",
        choices=["off", "auto", "required"],
        default="auto",
        help="Semantic parity mode passed to trace_gate.py.",
    )


def run_report_integrity(args: argparse.Namespace, report_path: Path, run_gate: GateRunner) -> None:
    run_gate(
        "correctness",
        [
            sys.executable,
            str(GATES / "check_correctness.py"),
            "--gates",
            args.gates,
            "--quirk",
            args.quirk,
            "--report",
            str(report_path),
        ],
    )
    run_gate(
        "trace",
        [
            sys.executable,
            str(GATES / "trace_gate.py"),
            "--report",
            str(report_path),
            "--semantic-parity-mode",
            args.trace_semantic_parity_mode,
        ],
    )
    if args.with_comparable_runtime_invariants_gate:
        run_gate(
            "comparable-runtime-invariants",
            [
                sys.executable,
                str(GATES / "comparable_runtime_invariants_gate.py"),
                "--report",
                str(report_path),
            ],
        )
    if args.with_compare_output_partition_gate:
        run_gate(
            "compare-output-partition",
            [
                sys.executable,
                str(GATES / "compare_output_partition_gate.py"),
                "--report",
                str(report_path),
            ],
        )


def run_report_comparison(args: argparse.Namespace, report_path: Path, run_gate: GateRunner) -> None:
    if args.with_comparability_coherence_gate:
        run_gate(
            "comparability-coherence",
            [
                sys.executable,
                str(GATES / "comparability_coherence_gate.py"),
                "--report",
                str(report_path),
                "--benchmark-policy",
                args.comparability_coherence_benchmark_policy,
                "--require-pass",
            ],
        )

    if args.with_structural_equivalence_gate:
        run_gate(
            "structural-equivalence",
            [
                sys.executable,
                str(GATES / "structural_equivalence_gate.py"),
                "--report",
                str(report_path),
                "--require-all-pass",
            ],
        )

    if args.with_backend_selection_gate:
        backend_policy_path = Path(args.backend_runtime_policy)
        if not backend_policy_path.exists():
            raise ValueError(f"FAIL: missing --backend-runtime-policy: {backend_policy_path}")
        backend_selection_command = [
            sys.executable,
            str(GATES / "backend_selection_gate.py"),
            "--report",
            str(report_path),
            "--policy",
            str(backend_policy_path),
        ]
        if args.backend_selection_lane.strip():
            backend_selection_command.extend(
                ["--lane", args.backend_selection_lane.strip()]
            )
        run_gate("backend-selection", backend_selection_command)

    if args.with_shader_artifact_gate:
        shader_schema_path = Path(args.shader_artifact_schema)
        if not shader_schema_path.exists():
            raise ValueError(f"FAIL: missing --shader-artifact-schema: {shader_schema_path}")
        spirv_val = args.shader_artifact_spirv_val.strip()
        if not spirv_val:
            spirv_val = shutil.which("spirv-val") or ""
        if args.shader_artifact_require_spirv_validation and not spirv_val:
            raise ValueError(
                "FAIL: --shader-artifact-require-spirv-validation "
                "requires --shader-artifact-spirv-val or spirv-val on PATH"
            )
        if spirv_val and shutil.which(spirv_val) is None:
            raise ValueError(f"FAIL: missing --shader-artifact-spirv-val executable: {spirv_val}")
        shader_artifact_command = [
            sys.executable,
            str(GATES / "shader_artifact_gate.py"),
            "--report",
            str(report_path),
            "--schema",
            str(shader_schema_path),
        ]
        if args.shader_artifact_require_manifest:
            shader_artifact_command.append("--require-manifest")
        if spirv_val:
            shader_artifact_command.extend(["--spirv-val", spirv_val])
        if args.shader_artifact_require_spirv_validation:
            shader_artifact_command.append("--require-spirv-validation")
        run_gate("shader-artifact", shader_artifact_command)


def run_report_backend_timing(args: argparse.Namespace, report_path: Path, run_gate: GateRunner) -> None:
    checks = (
        ("metal-sync", "metal", "sync_conformance_gate.py", args.with_metal_sync_conformance_gate),
        ("vulkan-sync", "vulkan", "sync_conformance_gate.py", args.with_vulkan_sync_conformance_gate),
        ("metal-timing-policy", "metal", "timing_policy_gate.py", args.with_metal_timing_policy_gate),
        ("vulkan-timing-policy", "vulkan", "timing_policy_gate.py", args.with_vulkan_timing_policy_gate),
    )
    for label, backend, script, enabled in checks:
        if enabled:
            run_gate(label, [
                sys.executable, str(GATES / script), "--backend", backend,
                "--report", str(report_path), "--timing-policy", args.backend_timing_policy,
            ])
