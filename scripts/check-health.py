#!/usr/bin/env python3
"""Run canonical Doe line/schema gates and repository health checks.

Zig policy belongs to runtime/zig/source-layout.json and its checker.
Schema coverage and validation belong to bench/gates/schema_gate.py. This entry
point owns Python size and retired root-directory checks.
"""

from __future__ import annotations

import subprocess
import sys
from collections.abc import Callable
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

PYTHON_DIRS = [REPO_ROOT / "bench", REPO_ROOT / "pipeline"]
PYTHON_MAX_LINES = 1200

ORPHAN_DIRS = ["fawn", "lean", "zig"]


def count_lines(path: Path) -> int:
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return sum(1 for _ in f)


GOVERNED_OVERSIZE_PYTHON = {
    "bench/browser/browser_gate.py",
    "bench/gates/claim_index_browser_release.py",
    "bench/gates/claim_index_browser_release_proof.py",
    "bench/lib/ecosystem_registry.py",
    "bench/native-compare/compare_doe_vs_tint_compilation.py",
    "bench/native_compare_modules/compare_assessment.py",
    "bench/runners/blocking_gates_args.py",
    "bench/runners/csl-runners/e2b_layer_block_smoke.py",
    "bench/runners/csl-runners/gemma_4_31b_layer_block_smoke.py",
    "bench/runners/csl-runners/manifest_dense_gemv_tiles.py",
    "bench/runners/csl-runners/manifest_kernel_probe_runner.py",
    "bench/tests/test_browser_benchmark_superset_checker.py",
    "bench/tests/test_browser_published_proof_surface.py",
    "bench/tests/test_browser_release_artifact_bundle.py",
    "bench/tests/test_browser_runtime_frontier_bundle.py",
    "bench/tests/test_claim_index_gate.py",
    "bench/tests/test_dawn_replacement_readiness_report.py",
    "bench/tests/test_int4ple_hostplan_runtime_timeout.py",
    "bench/tests/test_int4ple_scheduler_readiness.py",
    "bench/tests/test_manifest_kernel_probe_runner.py",
    "bench/tests/test_node_webgpu_executor.py",
    "bench/tools/build_dawn_replacement_readiness_report.py",
    "bench/tools/check_browser_published_proof_surface.py",
    "bench/tools/check_browser_release_artifact_bundle.py",
    "bench/tools/check_browser_release_package_inputs.py",
    "bench/tools/check_tint_compiler_frontier_bundle.py",
    "bench/tools/generate_e2b_layer_block_runner.py",
    "bench/tools/run_doe_csl_int4ple_transcript.py",
}


def run_gate(relative_path: str) -> list[str]:
    completed = subprocess.run(
        [sys.executable, str(REPO_ROOT / relative_path)],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode == 0:
        return []
    detail = (completed.stdout + completed.stderr).strip()
    return [detail or f"{relative_path} exited {completed.returncode}"]


def check_zig_sizes() -> list[str]:
    return run_gate("runtime/zig/tools/check_line_limits.py")


# ---------- check 2: Python file sizes ----------

def check_python_sizes() -> list[str]:
    failures: list[str] = []
    for d in PYTHON_DIRS:
        if not d.is_dir():
            continue
        for p in sorted(d.rglob("*.py")):
            # Scan owned source, excluding generated output and dependencies.
            try:
                rel_parts = p.relative_to(d).parts
            except ValueError:
                rel_parts = ()
            if any(
                part == "vendor" or part.startswith("vendor-")
                or part in {"out", "node_modules", ".venv", "__pycache__"}
                for part in rel_parts
            ):
                continue
            rel = str(p.relative_to(REPO_ROOT))
            if rel in GOVERNED_OVERSIZE_PYTHON:
                continue
            n = count_lines(p)
            if n > PYTHON_MAX_LINES:
                failures.append(f"  {rel}: {n} lines (max {PYTHON_MAX_LINES})")
    return failures


# ---------- check 3: Config schema coverage and validation ----------

def check_schema_validation() -> list[str]:
    return run_gate("bench/gates/schema_gate.py")


# ---------- check 4: Orphaned directories ----------

def check_orphaned_dirs() -> list[str]:
    failures: list[str] = []
    for name in ORPHAN_DIRS:
        d = REPO_ROOT / name
        if d.is_dir():
            failures.append(f"  {name}/: orphaned top-level directory exists")
    return failures


# ---------- runner ----------

def main() -> int:
    all_pass = True

    checks: list[tuple[str, Callable[[], list[str]]]] = [
        ("Zig source-layout line policy", check_zig_sizes),
        ("Python file sizes (bench/ + pipeline/, max 1200 lines)", check_python_sizes),
        ("Schema validation", check_schema_validation),
        ("Orphaned top-level directories", check_orphaned_dirs),
    ]

    for title, fn in checks:
        print(f"--- {title} ---")
        failures = fn()
        if failures:
            all_pass = False
            print("FAIL")
            for line in failures:
                print(line)
        else:
            print("PASS")
        print()

    if all_pass:
        print("All checks passed.")
        return 0
    else:
        print("Some checks failed.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
