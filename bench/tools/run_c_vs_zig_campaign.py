"""Orchestrate C/Zig catalog coverage through the existing run/compare owners.

Each provider runs separately with the same Doe-side workload parameters. Ordinary failed
rows remain in the ledger; unconfirmed GPU completion stops the cohort.
This tool does not implement timing selection or independent oracles. It refuses
performance comparison when a provider has no independent output evidence.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import signal
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
CATALOGS = {
    "vulkan": "bench/workloads/workloads.amd.vulkan.json",
    "metal": "bench/workloads/workloads.apple.metal.json",
    "d3d12": "bench/workloads/workloads.local.d3d12.json",
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def execute(command: list[str], log: Path, timeout: float, environment: dict[str, str]) -> dict[str, Any]:
    """Bound the CLI and its owned descendants; retain output even on failure."""
    options = ({"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == "nt"
               else {"start_new_session": True})
    with log.open("wb") as output:
        with subprocess.Popen(command, cwd=ROOT, env=environment, stdout=output, stderr=subprocess.STDOUT, **options) as child:
            try:
                code = child.wait(timeout=timeout)
                return {"command": command, "exitCode": code, "timedOut": False, "log": str(log)}
            except subprocess.TimeoutExpired:
                if child.poll() is None:
                    if os.name == "nt":
                        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], check=True, timeout=30,
                                       stdout=output, stderr=subprocess.STDOUT)
                    else:
                        os.killpg(child.pid, signal.SIGKILL)
                child.wait(timeout=30)
                return {"command": command, "exitCode": child.returncode, "timedOut": True, "log": str(log)}


def receipt_failures(receipt: dict[str, Any], implementation: str, iterations: int) -> list[str]:
    failures = []
    if receipt.get("runtimeIdentity", {}).get("runtimeImplementation") != implementation:
        failures.append("unexpected runtime implementation")
    execution = receipt.get("execution", {})
    if not execution.get("success") or execution.get("timedSampleCount") != iterations:
        failures.append("missing or unsuccessful timed samples")
    samples = receipt.get("samples", [])
    if len(samples) != iterations:
        failures.append("sample count differs from requested iterations")
    for sample in samples:
        meta = sample.get("traceMeta", {})
        if not sample.get("success") or sample.get("returnCode") != 0:
            failures.append("failed sample")
        if meta.get("fallbackUsed") is not False:
            failures.append("fallback state absent or enabled")
        for field in ("executionErrorCount", "executionUnsupportedCount", "executionSkippedCount", "outputOracleFailedCount"):
            if field not in meta or meta[field] != 0:
                failures.append(f"{field} absent or nonzero")
    return sorted(set(failures))


def oracle_failures(receipt: dict[str, Any]) -> list[str]:
    """Reject absent correctness evidence before requesting a speed comparison.

    Matching dispatch counters do not prove shader behavior. The canonical
    comparator still validates the declared command, reference and artifacts;
    this admission check prevents its optional-oracle path admitting these runs.
    """
    # The current native schema 1/2/3 oracles validate isolated execution,
    # including when invoked by a timed worker. No native same-stream capture
    # contract exists yet. Digests/counters alone must not invent that support.
    failures = [
        "native timed command-stream output proof is not implemented; "
        "isolated oracle validation cannot admit a performance comparison"
    ]
    for sample in receipt.get("samples", []):
        meta = sample.get("traceMeta", {})
        count = meta.get("outputOracleCount", 0)
        if (type(count) is not int or count <= 0
                or meta.get("outputOracleMatchedCount") != count
                or meta.get("outputOracleFailedCount") != 0):
            failures.append("positive successful output oracle missing")
        if meta.get("outputOracleReferenceClass") != "independent_v1":
            failures.append("independent output reference missing")
        actual = meta.get("outputOracleActualSha256", "")
        expected = meta.get("outputOracleExpectedSha256", "")
        has_actual = isinstance(actual, str) and bool(
            re.fullmatch(r"[0-9a-f]{64}", actual)
        )
        has_exact = (meta.get("outputOracleKind") == "sha256_exact_v1"
                     and has_actual and actual == expected)
        has_tolerance = False
        if meta.get("outputOracleKind") == "float32_reference_tolerance_v1":
            reference = meta.get("outputOracleReferencePath")
            reference_sha = meta.get("outputOracleReferenceSha256")
            values = meta.get("outputOracleComparedValueCount", 0)
            if isinstance(reference, str) and reference:
                path = (ROOT / reference).resolve()
                has_tolerance = (
                    has_actual and path.is_relative_to(ROOT.resolve())
                    and path.is_file() and digest(path) == reference_sha
                    and type(values) is int and values > 0
                    and meta.get("outputOracleMismatchCount") == 0
                )
        if not (has_exact or has_tolerance):
            failures.append("verified output digest or tolerance reference missing")
    if not receipt.get("samples"):
        failures.append("output oracle samples missing")
    return sorted(set(failures))


def unconfirmed_gpu_receipts(folder: Path) -> list[dict[str, Any]]:
    """Read final runtime state even when failed preflight emits no run receipt.

    These raw files belong to this fresh provider directory. A nonzero host exit
    alone is neither a proof of completion nor a reason to classify a GPU hang.
    """
    records = []
    for path in sorted(folder.rglob("*.meta.json")):
        try:
            meta = json.loads(path.read_bytes())
        except (OSError, json.JSONDecodeError):
            continue  # Unreadable metadata is not positive completion evidence.
        if not isinstance(meta, dict):
            continue
        if (meta.get("gpuCompletionUnknown") is True
                or meta.get("runtimeCloseComplete") is False):
            records.append({"path": str(path), "sha256": digest(path),
                            "gpuCompletionUnknown": meta.get("gpuCompletionUnknown"),
                            "runtimeCloseComplete": meta.get("runtimeCloseComplete")})
    return records


def row_result(row: dict[str, Any], output: Path, args: argparse.Namespace, index: int) -> dict[str, Any]:
    folder = output / "rows" / row["id"]
    folder.mkdir(parents=True)
    providers = [("doe-c", "c", f"doe_c_direct_{args.backend}"), ("doe", "zig", f"doe_direct_{args.backend}")]
    if index % 2:
        providers.reverse()
    result: dict[str, Any] = {"id": row["id"], "status": "running", "providerOrder": [p[0] for p in providers],
                              "providers": {}, "comparisons": {}, "failures": [],
                              "admissionFailures": [], "claimEligible": False}
    receipts = {}
    for product, implementation, executor in providers:
        environment = os.environ.copy()
        # This primary cohort measures fresh-process execution with persistent
        # C translation artifacts disabled. Warm/default-cache cohorts are separate.
        environment["DOE_SHADER_CACHE_DIR"] = ""
        command = [sys.executable, "bench/cli.py", "run", "--product", product, "--executor-id", executor,
                   "--workloads", args.catalog, "--workload-id", row["id"], "--iterations", str(args.iterations),
                   "--warmup", str(args.warmup), "--comparability-mode", "strict", "--require-timing-class", "operation",
                   "--resource-probe", "none", "--resource-sample-ms", "10", "--out", str(folder / product)]
        run = execute(command, folder / f"{product}.log", args.timeout, environment)
        run["deadlineScope"] = "provider-batch"
        run["deadlineSeconds"] = args.timeout
        if run["timedOut"]:
            run["timeoutInterpretation"] = (
                "Provider batch deadline expired; interrupted child GPU completion "
                "is unconfirmed. This does not establish a single-worker GPU hang."
            )
        artifacts = list((folder / product / "run-artifacts" / product).glob("*.run.json"))
        run["failures"] = []
        if run["exitCode"] != 0 or run["timedOut"] or len(artifacts) != 1:
            run["failures"].append("CLI failed, timed out, or did not emit exactly one receipt")
        else:
            path = artifacts[0]
            receipt = json.loads(path.read_bytes())
            run.update({"receipt": str(path), "receiptSha256": digest(path)})
            run["failures"] += receipt_failures(receipt, implementation, args.iterations)
            run["admissionFailures"] = oracle_failures(receipt)
            result["admissionFailures"] += [
                f"{product}: {failure}" for failure in run["admissionFailures"]
            ]
            if not run["failures"] and not run["admissionFailures"]:
                receipts[product] = path
        result["providers"][product] = run
        result["failures"] += [f"{product}: {failure}" for failure in run["failures"]]
        run["unconfirmedGpuReceipts"] = unconfirmed_gpu_receipts(folder / product)
        if run["timedOut"] or run["unconfirmedGpuReceipts"]:
            # Killing the host does not prove completion of submitted GPU work.
            # Stop this cohort rather than issuing more work on an unknown GPU.
            result["status"] = "gpu-completion-unknown"
            write_json(folder / "row.json", result)
            return result
    if len(receipts) == 2:
        command = [sys.executable, "bench/cli.py", "compare", str(receipts["doe-c"]), str(receipts["doe"]),
                   "--baseline-product", "doe-c", "--comparison-product", "doe", "--comparability", "strict",
                   "--out", str(folder / "comparison.json")]
        comparison = execute(command, folder / "compare.log", args.timeout, os.environ.copy())
        result["comparisons"]["operation"] = comparison
        if comparison["exitCode"] != 0 or not (folder / "comparison.json").exists():
            result["failures"].append("comparison did not complete")
        else:
            report = json.loads((folder / "comparison.json").read_bytes())
            result["comparisonStatus"] = report["comparisonStatus"]
            result["comparisonPath"] = str(folder / "comparison.json")
            result["comparisonSha256"] = digest(folder / "comparison.json")
    result["status"] = "failed" if result["failures"] else "executed"
    if result["status"] == "executed" and result["admissionFailures"]:
        result["status"] = "diagnostic"
    write_json(folder / "row.json", result)
    return result


def campaign(args: argparse.Namespace) -> int:
    output = Path(args.out).resolve()
    output.mkdir(parents=True, exist_ok=False)
    catalog = ROOT / args.catalog
    document = json.loads(catalog.read_bytes())
    rows = document["workloads"]
    if any(not isinstance(row.get("id"), str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", row["id"]) for row in rows):
        raise ValueError("workload IDs must be safe single directory names")
    if args.workload_id:
        rows = [row for row in rows if row["id"] in args.workload_id]
        if {row["id"] for row in rows} != set(args.workload_id):
            raise ValueError("requested workload not found in catalog")
    if len({row["id"] for row in rows}) != len(rows):
        raise ValueError("duplicate workload IDs in catalog")
    runtime_rows = [row for row in rows if row.get("runnerType", "zig-runtime") != "compilation"]
    if not runtime_rows:
        raise ValueError("no runtime rows selected; compiler rows use the separate compilation comparison")
    compiler_rows = [row["id"] for row in rows if row.get("runnerType") == "compilation"]
    suffix = ".exe" if os.name == "nt" else ""
    binaries = [ROOT / f"runtime/c/runtime-cli/bin/doe-c-runtime{suffix}",
                ROOT / f"runtime/zig/zig-out/bin/doe-zig-runtime{suffix}"]
    inputs = [catalog, ROOT / "config/benchmark-methodology-thresholds.json", *binaries,
              Path(__file__).resolve(), ROOT / "bench/cli.py"]
    inputs += [Path(path).resolve() for path in args.input_artifact]
    for row in runtime_rows:
        for name in ("commandsPath", "quirksPath"):
            if row.get(name):
                inputs.append(ROOT / row[name])
    # Source/hash checks cover the harness, kernels, selected runtime source and
    # direct compiler/backend dependencies, separately from the tested git ref.
    for directory in ("runtime/c", "runtime/zig/src", "bench/native_compare_modules", "bench/lib", "bench/kernels"):
        inputs += [p for p in (ROOT / directory).rglob("*") if p.is_file()
                   and p.suffix in (".c", ".h", ".m", ".zig", ".py", ".wgsl")
                   and not any(part in ("build", "__pycache__", "zig-out") for part in p.relative_to(ROOT).parts)]
    inputs = sorted(set(inputs))
    before = {str(path): digest(path) for path in inputs}
    ledger: dict[str, Any] = {
        "schemaVersion": 1, "scope": "native-runtime-catalog-c-versus-zig", "backend": args.backend,
        "startedAt": datetime.now(timezone.utc).isoformat(),
        "sourceCommit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "sourceStatus": subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).splitlines(),
        "host": {"platform": platform.platform(), "machine": platform.machine(), "node": platform.node()},
        "sampling": {"iterations": args.iterations, "warmup": args.warmup, "timeoutSecondsPerProvider": args.timeout,
                     "order": "alternating provider blocks by workload; serial on host",
                     "cache": "fresh CLI processes; warm filesystem; C translation persistence disabled; driver cache not purged; registry pipeline-cache flags",
                     "memory": "Windows owned-process peak working set; POSIX sampled process RSS plus GNU time when available; not aggregate tree or VRAM"},
        "inputs": before, "plannedRuntimeRows": [row["id"] for row in runtime_rows],
        "compilerRowsRequiringSeparateCompilerComparison": compiler_rows,
        "claimEligible": False, "rows": [], "status": "running",
    }
    write_json(output / "ledger.json", ledger)
    for index, row in enumerate(runtime_rows):
        print(f"[{index + 1}/{len(runtime_rows)}] {row['id']}", flush=True)
        try:
            result = row_result(row, output, args, index)
        except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
            result = {"id": row["id"], "status": "failed", "failures": [str(error)]}
        ledger["rows"].append(result)
        if result["status"] == "gpu-completion-unknown":
            ledger.update({"status": "gpu-completion-unknown", "stoppedAtRow": row["id"]})
            write_json(output / "ledger.json", ledger)
            return 1
        drift = [path for path, sha256 in before.items() if not Path(path).is_file() or digest(Path(path)) != sha256]
        if drift:
            ledger.update({"status": "source-drift", "changedInputs": drift})
            write_json(output / "ledger.json", ledger)
            return 1
        write_json(output / "ledger.json", ledger)
    failed = [row["id"] for row in ledger["rows"] if row["status"] == "failed"]
    diagnostic = [row["id"] for row in ledger["rows"] if row["status"] == "diagnostic"]
    ledger.update({"status": "failed" if failed else "executed", "failedRows": failed,
                   "diagnosticRows": diagnostic,
                   "endedAt": datetime.now(timezone.utc).isoformat()})
    write_json(output / "ledger.json", ledger)
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=CATALOGS, required=True)
    parser.add_argument("--catalog", default="")
    parser.add_argument("--out", required=True)
    parser.add_argument("--iterations", type=int, default=16)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=600,
                        help="Bound one complete provider batch, including warmup/preflight and all samples; not one GPU worker")
    parser.add_argument("--workload-id", action="append", default=[])
    parser.add_argument("--input-artifact", action="append", required=True,
                        help="Actual linked native/compiler library path; repeat to bind the whole built provider closure")
    args = parser.parse_args()
    if args.iterations < 1 or args.warmup < 0 or args.timeout <= 0:
        parser.error("invalid sampling or timeout")
    args.catalog = args.catalog or CATALOGS[args.backend]
    return campaign(args)


if __name__ == "__main__":
    raise SystemExit(main())
