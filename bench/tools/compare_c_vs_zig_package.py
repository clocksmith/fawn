"""Measure matched public package work with explicit C and Zig providers.

Canonical Node/Bun workers own execution and independent CPU validation. This
driver binds their inputs, rejects missing output proof or differing effective
paths, and preserves raw operation/process timings without promoting claims.
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import statistics
import struct
import subprocess
from typing import Any, Iterator

from bench.lib import synthetic_assets
from bench.tools.compare_c_vs_zig_compilation import process_sample, stats, write_json


ROOT = Path(__file__).resolve().parents[2]
PROFILES = {
    "node-package": ("node", "doe", False),
    "node-direct": ("node", "doe-direct", False),
    "bun-full": ("bun", "doe", False),
    "node-package-prepared": ("node", "doe", True),
    "node-direct-prepared": ("node", "doe-direct", True),
    "bun-full-prepared": ("bun", "doe", True),
}
SHAPES = {
    "package_vector_scale_add_262k": (1048576, None),
    "package_image_rgba_invert_1024": (4194304, None),
    "package_buffer_upload_readback_exact_1mb": (
        1048576, "77ddbb33d6d9ea7633eb24d312009bd35f6cffc0e716e09a9cb6a6ee4e367f01"),
    "package_pipeline_creation_8kernels": (32, None),
    "package_queue_submit_completion": (4, None),
}


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


@contextmanager
def environment(values: dict[str, str]) -> Iterator[None]:
    """Set only this sequential driver's child selectors outside the clock."""
    before = {key: os.environ.get(key) for key in values}
    os.environ.update(values)
    try:
        yield
    finally:
        for key, value in before.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


def sample_failures(meta: dict[str, Any], library: Path, arm: str,
                    prepared: bool, size: int, expected_sha: str | None) -> list[str]:
    failures = []
    identity = meta.get("packageProviderIdentity")
    identity = identity if isinstance(identity, dict) else {}
    selected = identity.get("doeLibraryPath")
    if (identity.get("loaded") is not True or identity.get("doeNative") is not True
            or identity.get("libraryFlavor") != ("doe-c" if arm == "c" else "doe-dropin")
            or not isinstance(selected, str) or Path(selected).resolve() != library.resolve()):
        failures.append("actual provider identity absent or different")
    for field in ("executionErrorCount", "executionUnsupportedCount", "executionSkippedCount"):
        if type(meta.get(field)) is not int or meta[field] != 0:
            failures.append(field + " absent or nonzero")
    if type(meta.get("executionSuccessCount")) is not int or meta["executionSuccessCount"] <= 0:
        failures.append("no successful commands")
    if (meta.get("timingClass") != "operation"
            or meta.get("packagePreparedSession") is not prepared
            or type(meta.get("timingMs")) not in (int, float)
            or not math.isfinite(meta["timingMs"]) or meta["timingMs"] <= 0):
        failures.append("operation timing or prepared scope absent or different")
    captures = meta.get("readbackCaptures", [])
    if (not isinstance(captures, list) or len(captures) != 1
            or not isinstance(captures[0], dict) or captures[0].get("byteLength") != size
            or not re.fullmatch(r"[0-9a-f]{64}", str(captures[0].get("sha256", "")))):
        failures.append("full independent oracle readback absent")
    elif expected_sha and captures[0].get("sha256") != expected_sha:
        failures.append("independent output digest mismatch")
    if not meta.get("packageEffectiveReadbackPaths") or not meta.get("packageReadbackActualPaths"):
        failures.append("effective readback path missing")
    if (meta.get("adapterInfo") or {}).get("isFallbackAdapter") is not False:
        failures.append("physical non-fallback adapter not established")
    return failures


def pair_failures(left: dict[str, Any], right: dict[str, Any]) -> list[str]:
    fields = ("planHash", "executionShape", "executionDispatchCount", "executionSubmitCount",
              "packagePreparedSession", "packageSetupIncludedInSelectedTiming",
              "packageEffectiveReadbackPaths", "packageReadbackActualPaths", "queueWaitMode")
    failures = [field + " missing or differs" for field in fields
                if field not in left or field not in right or left[field] != right[field]]
    if not (left.get("adapterInfo") or {}).get("device") or (
            left.get("adapterInfo", {}).get("device") != right.get("adapterInfo", {}).get("device")):
        failures.append("physical adapter identity differs")
    return failures


class OwnedWorkers:
    """Stop only this driver's current session and restore signal ownership."""

    def __init__(self, report: dict[str, Any]) -> None:
        self.child: subprocess.Popen | None = None
        self.signals: list[int] = []
        self.previous: dict[int, Any] = {}
        self.report = report

    def cancel(self, signum: int, _frame: Any) -> None:
        if not self.signals:
            self.signals.append(signum)
        if self.child is not None and self.child.poll() is None:
            self.report["cancelledGpuCompletionUnknown"] = True
            if os.name == "nt":
                self.child.kill()
            else:
                try:
                    os.killpg(self.child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass

    def started(self, child: subprocess.Popen) -> None:
        self.child = child
        if self.signals:
            self.cancel(self.signals[0], None)

    def __enter__(self) -> OwnedWorkers:
        for signum in (signal.SIGTERM, signal.SIGINT):
            self.previous[signum] = signal.signal(signum, self.cancel)
        return self

    def __exit__(self, *_exception: Any) -> None:
        if self.child is not None and self.child.poll() is None:
            self.cancel(signal.SIGTERM, None)
            self.child.wait()
        for signum, handler in self.previous.items():
            signal.signal(signum, handler)


def cases(oracle_plans: Path, profiles: list[str]) -> list[dict[str, Any]]:
    manifest = json.loads((oracle_plans / "oracle-manifest.json").read_text())
    prospective = {row["workloadId"]: row for row in manifest["plans"]}
    result = []
    for profile in profiles:
        host, provider, prepared = PROFILES[profile]
        suffix = ".prepared" if prepared else ""
        catalog = ROOT / f"bench/workloads/workloads.package.developer{suffix}.json"
        for row in json.loads(catalog.read_text())["workloads"]:
            workload = row["id"]
            size, expected = SHAPES[workload]
            plan_path = ROOT / row["planPath"]
            if workload in prospective:
                item = prospective[workload]
                plan_path = Path(item["plan"])
                if (digest(plan_path) != item["planSha256"] or item["fullOutputBytes"] != size
                        or item["referenceClass"] != "independent_cpu_v1"
                        or digest(ROOT / row["planPath"]) != item["sourcePlanSha256"]
                        or digest(ROOT / item["shaderPath"]) != item["shaderSha256"]):
                    raise ValueError("prospective plan differs from its independent CPU receipt")
            plan = json.loads(plan_path.read_text())
            if workload == "package_vector_scale_add_262k":
                reference = plan["commands"][-1]["captureValidate"]
                if reference["kind"] != "f32BufferOneOfV1" or len(reference["values"]) * 4 != size:
                    raise ValueError("vector full-span reference missing")
            elif workload == "package_image_rgba_invert_1024":
                expected = plan["commands"][-1]["captureValidate"]["sha256"]
            elif not expected:
                values = plan["steps"][-1]["validate"]["values"]
                expected = hashlib.sha256(struct.pack("<" + "I" * len(values), *values)).hexdigest()
            result.append({"id": workload, "profile": profile, "host": host,
                           "provider": provider, "prepared": prepared, "plan": str(plan_path.resolve()),
                           "catalog": str(catalog), "fullOutputBytes": size, "expectedSha256": expected})
    return result


def configuration_inputs(root: Path) -> set[Path]:
    """Bind worker policy/module configuration before starting any GPU worker."""
    return {(root / name).resolve(strict=True) for name in (
        "config/package-execution-policy.json", "config/determinism-policy.json",
        "config/numeric-stability-policy.json", "packages/doe-gpu/package.json")}


def run(args: argparse.Namespace) -> int:
    if args.iterations < 1 or args.warmup < 0:
        raise ValueError("iterations must be positive and warmup nonnegative")
    selected = cases(args.oracle_plans.resolve(), args.profiles)
    libraries = {"c": args.c_library.resolve(), "zig": args.zig_library.resolve()}
    build = json.loads(args.build_inputs.read_text())
    paths = {args.build_inputs.resolve(), Path(__file__).resolve(),
             ROOT / "bench/tools/compare_c_vs_zig_compilation.py", ROOT / "bench/lib/child_usage.py",
             ROOT / "bench/tools/prepare_package_oracle_plans.py", ROOT / "bench/lib/synthetic_assets.py",
             args.oracle_plans.resolve() / "oracle-manifest.json"}
    paths.update(configuration_inputs(ROOT))
    manifest = json.loads((args.oracle_plans / "oracle-manifest.json").read_text())
    if manifest["generatorSha256"] != digest(ROOT / "bench/tools/prepare_package_oracle_plans.py"):
        raise ValueError("CPU reference generator differs from its receipt")
    hosts = {row["host"]: Path(shutil.which(row["host"]) or row["host"]).resolve() for row in selected}
    paths.update(hosts.values())
    for name, info in build["artifacts"].items():
        path = Path(name).resolve()
        if digest(path) != info["sha256"]:
            raise ValueError(f"build artifact differs from its receipt: {path}")
        paths.add(path)
    for library in libraries.values():
        if str(library) not in build["artifacts"]:
            raise ValueError("explicit library must appear in the actual build-input receipt")
    for subtree in ("bench/executors", "packages/doe-gpu/src", "runtime/bridge/webgpu-addon"):
        paths.update(path for path in (ROOT / subtree).rglob("*")
                     if path.is_file() and path.suffix in (".js", ".c", ".h"))
    paths.update(path for path in (ROOT / "bench/kernels").rglob("*.wgsl"))
    for row in selected:
        paths.update((Path(row["plan"]), Path(row["catalog"])))
        for asset in synthetic_assets.describe_plan_assets(Path(row["plan"])):
            path = Path(asset["assetPath"])
            generated = synthetic_assets.generate_asset_bytes(
                generator=asset["generator"], seed=asset["seed"],
                scale=asset["scale"], byte_length=asset["byteLength"])
            if digest(path) != hashlib.sha256(generated).hexdigest():
                raise ValueError("actual input asset differs from its frozen CPU generator")
            paths.update((path, Path(asset["manifestPath"])))
    inputs = {str(path): digest(path) for path in sorted(paths)}
    args.out.mkdir(parents=True, exist_ok=False)
    report = {"schemaVersion": 1, "scope": "same public package work, fixed N-API backend, independent full output references",
              "claimEligible": False, "sourceCommit": subprocess.check_output(
                  ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "sourceStatus": subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).splitlines(),
              "startedAt": datetime.now(timezone.utc).isoformat(), "inputs": inputs,
              "host": {"platform": platform.platform(), "machine": platform.machine(),
                       "nodeBunExecutables": {key: str(value) for key, value in hosts.items()}},
              "sampling": {"iterations": args.iterations, "warmup": args.warmup,
                           "order": "alternate provider order per paired sample and case",
                           "operation": "canonical worker timingMs; prepared scope preserved",
                           "process": "fresh worker process through exit; no supervisor or subtraction",
                           "memory": "1ms RSS observations are lower bounds, not peaks or VRAM"},
              "childSelectors": {"DOE_BUN_WEBGPU_BACKEND": "full", "DOE_MTL_ALLOW_HARDWARE": "1",
                                 "DOE_WEBGPU_LIB": {arm: str(lib) for arm, lib in libraries.items()},
                                 "DOE_NODE_WEBGPU_CHILD": "1", "DOE_BUN_WEBGPU_CHILD": "1"},
              "rows": [], "status": "running"}
    write_json(args.out / "report.json", report)
    workers = OwnedWorkers(report)
    cancellation = workers.signals
    stopped = False
    with workers:
        for index, case in enumerate(selected):
            if cancellation:
                break
            print(f"[{index+1}/{len(selected)}] {case['profile']} {case['id']}", flush=True)
            row = {**case, "providers": {arm: {"samples": [], "warmup": []} for arm in libraries}, "failures": []}
            for sample_index in range(args.warmup + args.iterations):
                paired = {}
                order = ("c", "zig") if (index + sample_index) % 2 == 0 else ("zig", "c")
                for arm in order:
                    folder = args.out / "samples" / f"{index:03d}" / f"{sample_index:03d}-{arm}"
                    command = [str(hosts[case["host"]]),
                               str(ROOT / f"bench/executors/run-{case['host']}-webgpu-plan.js"),
                               "--provider", case["provider"], "--plan", case["plan"], "--workload", case["id"],
                               "--trace-meta", str(folder / "meta.json"), "--trace-jsonl", str(folder / "trace.jsonl")]
                    if case["prepared"]:
                        command.append("--prepared-session")
                    with environment({"DOE_WEBGPU_LIB": str(libraries[arm]), "DOE_BUN_WEBGPU_BACKEND": "full",
                                      "DOE_NODE_WEBGPU_CHILD": "1", "DOE_BUN_WEBGPU_CHILD": "1",
                                      "DOE_MTL_ALLOW_HARDWARE": "1"}):
                        try:
                            sample = process_sample(command, folder, args.timeout, child_started=workers.started)
                        except OSError as error:
                            sample = {"command": command, "exitCode": None, "timedOut": False,
                                      "error": str(error), "wallNs": 0}
                        finally:
                            workers.child = None
                    meta_path = folder / "meta.json"
                    try:
                        meta = json.loads(meta_path.read_text())
                    except (OSError, ValueError):
                        meta = {}
                    sample["traceMeta"] = str(meta_path)
                    sample["traceMetaSha256"] = digest(meta_path) if meta_path.is_file() else None
                    trace_path = folder / "trace.jsonl"
                    sample["traceJsonlSha256"] = digest(trace_path) if trace_path.is_file() else None
                    sample["providerOrder"] = list(order)
                    sample["sampleIndex"] = sample_index
                    sample["operationMs"] = meta.get("timingMs")
                    failures = sample_failures(meta, libraries[arm], arm, case["prepared"], case["fullOutputBytes"], case["expectedSha256"])
                    if sample["exitCode"] != 0 or sample["timedOut"]:
                        failures.append("failed or timed-out process")
                    sample["failures"] = failures
                    row["failures"].extend(arm + ": " + failure for failure in failures)
                    row["providers"][arm]["warmup" if sample_index < args.warmup else "samples"].append(sample)
                    paired[arm] = meta
                    if sample["timedOut"]:
                        stopped = True
                        row["failures"].append("GPU completion unknown after owned process timeout; later work stopped")
                        break
                    if cancellation:
                        row["failures"].append("owned worker cancelled; no later GPU work")
                        break
                if len(paired) == 2:
                    row["failures"].extend(pair_failures(paired["c"], paired["zig"]))
                if stopped or cancellation:
                    break
            for group in row["providers"].values():
                group["processStatistics"] = stats(group["samples"])
                values = [s["operationMs"] for s in group["samples"] if not s["failures"]]
                group["operationStatistics"] = stats([
                    {"wallNs": round(value * 1000000), "exitCode": 0, "timedOut": False} for value in values])
                for field in ("cpuMs", "sampledResidentLowerBoundKiB"):
                    observed = [sample[field] for sample in group["samples"] if sample.get(field) is not None]
                    group[field + "Median"] = statistics.median(observed) if observed else None
            row["failures"] = sorted(set(row["failures"]))
            row["status"] = "incomparable" if row["failures"] else "comparable_public_api_work"
            if not row["failures"]:
                row["zigOverCOperationMedianRatio"] = (
                    row["providers"]["zig"]["operationStatistics"]["p50Ns"] /
                    row["providers"]["c"]["operationStatistics"]["p50Ns"])
            report["rows"].append(row)
            report["status"] = "stopped_gpu_completion_unknown" if stopped else "running"
            report["drift"] = [path for path, sha in inputs.items()
                               if not Path(path).is_file() or digest(Path(path)) != sha]
            write_json(args.out / "report.json", report)
            if stopped or cancellation or report["drift"]:
                break
    report["drift"] = [path for path, sha in inputs.items() if not Path(path).is_file() or digest(Path(path)) != sha]
    if report["drift"]:
        for row in report["rows"]:
            row["status"] = "incomparable"
            row["failures"].append("input drift invalidates this campaign")
            row.pop("zigOverCOperationMedianRatio", None)
    if cancellation:
        report["status"] = ("cancelled_gpu_completion_unknown" if report.get("cancelledGpuCompletionUnknown") else "cancelled")
        report["cancellationSignals"] = cancellation
    elif not stopped:
        report["status"] = "compared_with_gaps" if report["drift"] or any(r["failures"] for r in report["rows"]) else "compared"
    report["endedAt"] = datetime.now(timezone.utc).isoformat()
    write_json(args.out / "report.json", report)
    return int(report["status"] != "compared")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--c-library", required=True, type=Path, help="Exact built C native provider.")
    parser.add_argument("--zig-library", required=True, type=Path, help="Exact fresh Zig full native provider.")
    parser.add_argument("--build-inputs", required=True, type=Path, help="Hash-bound actual library/dependency/addon build receipt.")
    parser.add_argument("--oracle-plans", required=True, type=Path, help="Prospective independent CPU oracle plan directory.")
    parser.add_argument("--out", required=True, type=Path, help="Fresh raw measurement directory.")
    parser.add_argument("--profiles", choices=list(PROFILES), nargs="+", default=list(PROFILES), help="Package API treatments.")
    parser.add_argument("--iterations", type=int, default=16, help="Measured fresh worker processes per provider/case.")
    parser.add_argument("--warmup", type=int, default=1, help="Excluded fresh worker warmups per provider/case.")
    parser.add_argument("--timeout", type=float, default=30, help="Per-worker bound; unknown GPU completion stops later work.")
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
