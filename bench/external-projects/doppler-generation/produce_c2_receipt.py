"""Produce zero-trust C2 receipts around Doppler complete generation (X1 exact-token oracle).

Independently logs selected backend, browser/runtime flags, sandbox state,
loaded native runtime library SHA-256s, exact prompt/generated token IDs and
stop reasons against ``complete-generation-v2.json``, bounded timeouts, and
verified zero-orphan process-group cleanup on failed or timed-out launches.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

CONTRACT_REL = "bench/external-projects/doppler-generation/complete-generation-v2.json"
FORBIDDEN_SANDBOX_FLAGS = frozenset({"--no-sandbox", "--disable-setuid-sandbox"})


def sha256_file(path: Path) -> str:
    """Return lowercase SHA-256 hex digest of a file."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def list_process_group_members(pgid: int) -> list[int]:
    """Return live PIDs belonging to process group ``pgid`` from the OS process table."""
    if pgid <= 0:
        return []
    try:
        out = subprocess.check_output(["ps", "-eo", "pid=,pgid="], text=True, timeout=5)
    except Exception:
        return []
    survivors: list[int] = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) != 2:
            continue
        try:
            pid_val = int(parts[0])
            pgid_val = int(parts[1])
        except ValueError:
            continue
        if pgid_val == pgid:
            survivors.append(pid_val)
    return sorted(survivors)


def run_bounded_process_with_orphan_check(
    argv: list[str],
    *,
    cwd: Path,
    env: dict[str, str] | None = None,
    timeout_seconds: float = 30.0,
) -> dict[str, Any]:
    """Run ``argv`` in an isolated process group, reaping descendants and verifying 0 orphans."""
    merged_env = dict(os.environ)
    if env:
        merged_env.update(env)

    t0 = time.monotonic()
    proc = subprocess.Popen(
        argv,
        cwd=str(cwd),
        env=merged_env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        start_new_session=True,
    )
    pgid = proc.pid
    timed_out = False
    stdout_str = ""
    stderr_str = ""

    try:
        stdout_str, stderr_str = proc.communicate(timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        timed_out = True
        try:
            os.killpg(pgid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            pass
        time.sleep(0.05)
        try:
            os.killpg(pgid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass
        for member_pid in list_process_group_members(pgid):
            try:
                os.kill(member_pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass
        stdout_str, stderr_str = proc.communicate()
    finally:
        # Ensure any background child spawned inside the session group is also reaped
        for member_pid in list_process_group_members(pgid):
            try:
                os.kill(member_pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass
        if list_process_group_members(pgid):
            time.sleep(0.05)

    surviving_orphans = list_process_group_members(pgid)
    elapsed_ms = round((time.monotonic() - t0) * 1000.0, 3)
    return {
        "argv": list(argv),
        "exitCode": proc.returncode,
        "timedOut": timed_out,
        "elapsedMs": elapsed_ms,
        "processGroupId": pgid,
        "survivingOrphanPids": surviving_orphans,
        "zeroOrphansVerified": len(surviving_orphans) == 0,
        "stdoutPreview": stdout_str[:1000],
        "stderrPreview": stderr_str[:1000],
    }


def verify_doppler_oracle_cases(
    contract: dict[str, Any],
    observed_cases: dict[str, dict[str, Any]],
    reference_cases: dict[str, dict[str, Any]],
) -> list[str]:
    """Verify exact prompt IDs, generated IDs, decoded text, and stop reason per case."""
    errors: list[str] = []
    for cspec in contract["cases"]:
        cid = cspec["id"]
        obs = observed_cases.get(cid)
        ref = reference_cases.get(cid)
        if not isinstance(obs, dict) or not isinstance(ref, dict):
            errors.append(f"missing observed or reference case: {cid}")
            continue
        for key in ("promptTokenIds", "generatedTokenIds", "decodedText", "stopReason"):
            if obs.get(key) != ref.get(key) or obs.get(key) in (None, "", []):
                errors.append(f"{cid}: mismatch on {key}")
    return errors


def build_doppler_workload_row(
    *,
    repo_root: Path,
    model_dir: Path,
    observed_cases: dict[str, dict[str, Any]],
    reference_cases: dict[str, dict[str, Any]],
    dawn_samples_ms: list[float],
    doe_samples_ms: list[float],
    cold_setup_ms: float,
    command_prep_ms: float,
    cpu_time_ms: float,
    rss_bytes: int,
    gpu_allocated_bytes: int,
    fail_or_skip_count: int = 0,
) -> dict[str, Any]:
    """Construct a hash-verified C2 ``doppler_complete_generation`` workload row."""
    contract = json.loads((repo_root / CONTRACT_REL).read_text(encoding="utf-8"))
    observed_hashes: dict[str, str] = {}
    for fentry in contract["model"]["files"]:
        rel = fentry["path"]
        fpath = model_dir / rel
        if not fpath.is_file():
            raise FileNotFoundError(f"missing Doppler model artifact: {fpath}")
        observed_hashes[rel] = sha256_file(fpath)

    oracle_errors = verify_doppler_oracle_cases(contract, observed_cases, reference_cases)
    if oracle_errors:
        raise ValueError(f"Doppler X1 exact-token oracle failed: {oracle_errors}")

    ordered_doe = sorted(float(x) for x in doe_samples_ms)
    median_doe = ordered_doe[len(ordered_doe) // 2]
    return {
        "workloadId": "doppler_complete_generation",
        "observedSourceFileHashes": observed_hashes,
        "observedCases": observed_cases,
        "referenceCases": reference_cases,
        "dawnSamplesMs": [float(x) for x in dawn_samples_ms],
        "doeSamplesMs": [float(x) for x in doe_samples_ms],
        "timingPhasesMs": {
            "coldSetupMs": float(cold_setup_ms),
            "commandPreparationMs": float(command_prep_ms),
            "cpuVisibleCompletionMs": float(median_doe),
        },
        "resources": {
            "cpuTimeMs": float(cpu_time_ms),
            "processRssBytes": int(rss_bytes),
            "gpuAllocatedBytes": int(gpu_allocated_bytes),
            "failOrSkipCount": int(fail_or_skip_count),
        },
    }


def build_doppler_producer_metadata(
    *,
    backend_id: str,
    browser_args: list[str],
    doe_library_path: Path,
    observed_loaded_libraries: list[dict[str, str]],
) -> dict[str, Any]:
    """Validate and summarize backend, flags, sandbox state, and runtime library identity."""
    if backend_id not in ("amd-vulkan", "apple-metal"):
        raise ValueError(f"unsupported backend_id: {backend_id!r}")
    sandbox_disabled = any(arg.split("=", 1)[0] in FORBIDDEN_SANDBOX_FLAGS for arg in browser_args)
    if sandbox_disabled:
        raise ValueError("sandbox-disabling flags are forbidden in C2 Doppler receipts")
    doe_sha = sha256_file(doe_library_path)
    loaded_shas = {entry["sha256"] for entry in observed_loaded_libraries}
    if doe_sha not in loaded_shas:
        raise ValueError("expected Doe library SHA-256 was not observed in process maps")
    if any("dawn" in entry.get("path", "").lower() for entry in observed_loaded_libraries):
        raise ValueError("Dawn fallback library detected in Doe process maps")
    return {
        "backendId": backend_id,
        "sandboxState": "enabled",
        "actualBrowserArgs": list(browser_args),
        "expectedDoeLibrarySha256": doe_sha,
        "observedLoadedLibraries": observed_loaded_libraries,
    }


def run_selftest_failed_launch(repo_root: Path, out_path: Path) -> dict[str, Any]:
    """Spawn a hanging child + background grandchild on CPU and prove 0 surviving orphans."""
    py_code = (
        "import subprocess, sys, time; "
        "subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)']); "
        "time.sleep(30)"
    )
    result = run_bounded_process_with_orphan_check(
        [sys.executable, "-c", py_code],
        cwd=repo_root,
        timeout_seconds=0.25,
    )
    receipt = {
        "schema": "fawn.c2-doppler-failed-launch-diagnostic/v1",
        "claimable": False,
        "diagnosticReason": "simulated-launch-timeout-with-grandchild",
        "launchOutcome": result,
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(f"{json.dumps(receipt, indent=2)}\n", encoding="utf-8")
    if not result["timedOut"] or not result["zeroOrphansVerified"]:
        raise RuntimeError(f"failed-launch orphan check failed: {result}")
    return receipt


def main(argv: list[str] | None = None) -> int:
    """CLI entrypoint."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default="", help="Optional Fawn repository root.")
    parser.add_argument("--backend", default="amd-vulkan", choices=["amd-vulkan", "apple-metal"])
    parser.add_argument("--plan-only", action="store_true", help="Print Doppler C2 producer plan.")
    parser.add_argument(
        "--selftest-failed-launch",
        action="store_true",
        help="Run CPU failed-launch timeout + zero-orphan process-group reaper test.",
    )
    parser.add_argument("--out", default="", help="Output path for diagnostic or receipt JSON.")
    args = parser.parse_args(argv)

    repo_root = Path(args.root).resolve() if args.root else Path(__file__).resolve().parents[3]
    contract = json.loads((repo_root / CONTRACT_REL).read_text(encoding="utf-8"))

    if args.plan_only:
        plan = {
            "schema": "fawn.c2-doppler-receipt-producer-plan/v1",
            "backendId": args.backend,
            "contractPath": CONTRACT_REL,
            "cases": [c["id"] for c in contract["cases"]],
            "independentProcessesPerLabel": contract["latencyCampaign"]["independentProcessesPerLabel"],
            "sandboxRequired": True,
            "oracle": "exact_prompt_ids_generated_ids_decoded_text_and_stop_reason",
            "orphanCheck": "isolated_process_group_reaped_and_verified_via_ps",
        }
        print(json.dumps(plan, indent=2))
        return 0

    if args.selftest_failed_launch:
        if not args.out:
            raise SystemExit("--selftest-failed-launch requires --out <path>")
        diag = run_selftest_failed_launch(repo_root, Path(args.out).resolve())
        print(json.dumps({"status": "ok", "zeroOrphansVerified": diag["launchOutcome"]["zeroOrphansVerified"]}))
        return 0

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
