"""Offline admission checker for Doe C2 WebGPU graphics, Metal tail, and Chromium ownership campaigns.

Validates ``config/webgpu-graphics-proof-campaigns.json`` against repository
authorities and evaluates candidate Vulkan, Metal, and forced-Doe Chromium
receipts without requiring a physical GPU.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

from bench.lib.bench_utils import is_safe_repo_path, load_json_object

CONTRACT_REL_PATH = "config/webgpu-graphics-proof-campaigns.json"
REQUIRED_WORKLOAD_FAMILIES = frozenset(
    {
        "inference_compute",
        "three_webgpu_scene",
        "image_filter_2d",
        "particles_or_fluids",
        "texture_upload_presentation",
    }
)


def resolve_repo_root(explicit_root: str | Path | None = None) -> Path:
    """Return the repository root path."""
    if explicit_root:
        return Path(explicit_root).resolve()
    return Path(__file__).resolve().parents[2]


def validate_campaign_contract(repo_root: Path) -> list[str]:
    """Validate the frozen C2 campaign contract against its authority files.

    Parameters
    ----------
    repo_root : Path
        Root of the Fawn repository.

    Returns
    -------
    list[str]
        List of policy failure descriptions; empty when valid.
    """
    failures: list[str] = []
    contract_path = repo_root / CONTRACT_REL_PATH
    if not contract_path.exists():
        return [f"missing contract: {CONTRACT_REL_PATH}"]

    contract = load_json_object(contract_path)
    for key in (
        "strategyAuthorityPath",
        "thesisAuthorityPath",
        "methodologyThresholdsPath",
        "integratedComparisonPath",
    ):
        rel = contract.get(key)
        if not is_safe_repo_path(rel):
            failures.append(f"{key} is not a safe repository-relative path: {rel!r}")
            continue
        if not (repo_root / rel).exists():
            failures.append(f"{key} target does not exist: {rel}")

    boundary = contract.get("nonConfoundingBoundary", {})
    if boundary.get("workspaceEvidenceQualifiesDoe") is not False:
        failures.append("C1 workspace evidence must never qualify Doe")
    if boundary.get("kernelWinQualifiesPageRendering") is not False:
        failures.append("isolated kernel speedups must never qualify page rendering")
    if boundary.get("forbidCrossBackendAveraging") is not True:
        failures.append("cross-backend averaging must remain forbidden")

    workloads = contract.get("workloads", [])
    seen_families: set[str] = set()
    for item in workloads:
        wid = item.get("workloadId", "<unknown>")
        fam = item.get("family", "")
        seen_families.add(fam)
        src_rel = item.get("sourceContractPath")
        if src_rel is not None:
            if not is_safe_repo_path(src_rel) or not (repo_root / src_rel).exists():
                failures.append(f"{wid}: invalid sourceContractPath {src_rel!r}")
        oracle_ref = item.get("oracle", {}).get("referencePath")
        if not is_safe_repo_path(oracle_ref) or not (repo_root / oracle_ref).exists():
            failures.append(f"{wid}: invalid oracle.referencePath {oracle_ref!r}")
        if fam != "inference_compute" and item.get("requiresDisplayedCanvasPresentation") is not True:
            failures.append(f"{wid}: graphics family {fam} must require displayed canvas presentation")

    missing_families = REQUIRED_WORKLOAD_FAMILIES - seen_families
    if missing_families:
        failures.append(f"missing required workload families: {sorted(missing_families)}")

    backends = {b.get("backendId"): b for b in contract.get("backends", [])}
    if "amd-vulkan" not in backends or "apple-metal" not in backends:
        failures.append("both amd-vulkan and apple-metal backends must be declared")
    else:
        metal = backends["apple-metal"]
        if metal.get("dependsOnFrozenBackendId") != "amd-vulkan":
            failures.append("apple-metal must sequence after frozen amd-vulkan checkpoint")
        obligations = metal.get("tailRegressionObligations", [])
        if not any(o.get("obligationId") == "onnx-runtime-metal-tail-p95-p99" for o in obligations):
            failures.append("apple-metal must retain onnx-runtime-metal-tail-p95-p99 obligation")

    ownership = contract.get("chromiumOwnershipGate", {})
    maint_rel = ownership.get("maintainabilityAuthorityPath")
    if not is_safe_repo_path(maint_rel) or not (repo_root / maint_rel).exists():
        failures.append(f"invalid maintainabilityAuthorityPath: {maint_rel!r}")
    integrated_path = repo_root / str(contract.get("integratedComparisonPath", ""))
    if integrated_path.exists():
        integrated = load_json_object(integrated_path)
        expected_arms = [arm.get("name") for arm in integrated.get("arms", [])]
        if ownership.get("threeWayArmsInOrder") != expected_arms:
            failures.append("threeWayArmsInOrder does not match integrated comparison arms")

    return failures


def evaluate_backend_campaign_receipt(
    contract: dict[str, Any],
    thresholds: dict[str, Any],
    receipt: dict[str, Any],
) -> list[str]:
    """Evaluate a candidate Vulkan or Metal application campaign receipt.

    Parameters
    ----------
    contract : dict[str, Any]
        Parsed ``webgpu-graphics-proof-campaigns.json`` object.
    thresholds : dict[str, Any]
        Parsed ``benchmark-methodology-thresholds.json`` object.
    receipt : dict[str, Any]
        Candidate backend campaign receipt to check.

    Returns
    -------
    list[str]
        List of admission blockers; empty when admissible.
    """
    failures: list[str] = []
    controls = contract["comparatorControls"]
    suspicious_ratio = float(thresholds["reliability"]["suspiciousSpeedupRatio"])
    min_samples = int(thresholds["claimabilityDefaults"]["localMinTimedSamples"])

    backend_id = receipt.get("backendId")
    backends_by_id = {b["backendId"]: b for b in contract["backends"]}
    if backend_id not in backends_by_id:
        return [f"unknown backendId: {backend_id!r}"]
    backend_spec = backends_by_id[backend_id]

    if receipt.get("pooledAcrossBackends") is True:
        failures.append("cross-backend pooling is forbidden")
    if backend_spec["dependsOnFrozenBackendId"] and not receipt.get("priorBackendCheckpointFrozen"):
        failures.append(
            f"{backend_id} requires frozen {backend_spec['dependsOnFrozenBackendId']} checkpoint first"
        )
    if not receipt.get("sourceMatchedDawn"):
        failures.append("Dawn baseline must be source-matched")
    if not receipt.get("identicalValidationCacheAndWarmup"):
        failures.append("validation, cache, and warmup policies must match Dawn")
    if receipt.get("fallbackOccurred") is True:
        failures.append("fallback to Dawn occurred during Doe campaign")

    aa_runs = int(receipt.get("aaIndependentRuns", 0))
    aa_drift = float(receipt.get("aaP50DriftRatio", 999.0))
    if aa_runs < int(controls["minAaRunsPerBackend"]):
        failures.append(f"insufficient A/A runs: {aa_runs} < {controls['minAaRunsPerBackend']}")
    if aa_drift > float(controls["maxAaP50DriftRatio"]):
        failures.append(f"A/A p50 drift {aa_drift:.3f} exceeds {controls['maxAaP50DriftRatio']}")

    rows_by_id = {r.get("workloadId"): r for r in receipt.get("workloadRows", [])}
    for wspec in contract["workloads"]:
        wid = wspec["workloadId"]
        row = rows_by_id.get(wid)
        if row is None:
            failures.append(f"missing required workload row: {wid}")
            continue
        if row.get("substitutedMicrobenchmark") is True or row.get("unchangedApplication") is not True:
            failures.append(f"{wid}: microbenchmark substitution or modified application is forbidden")
        if row.get("oraclePassed") is not True:
            failures.append(f"{wid}: independent correctness oracle did not pass")
        if int(row.get("timedSamples", 0)) < min_samples:
            failures.append(f"{wid}: timedSamples < {min_samples}")
        if wspec.get("requiresDisplayedCanvasPresentation"):
            if row.get("displayedCanvasVerified") is not True:
                failures.append(f"{wid}: missing displayed canvas presentation verification")
            if row.get("timingMethod") in controls["forbiddenPresentationSubstitutes"]:
                failures.append(f"{wid}: forbidden timing substitute {row.get('timingMethod')!r}")

        phases = row.get("timingPhasesMs", {})
        for req_phase in controls["requiredTimingPhases"]:
            if req_phase == "displayedPresentationMs" and not wspec.get("requiresDisplayedCanvasPresentation"):
                continue
            if not isinstance(phases.get(req_phase), (int, float)):
                failures.append(f"{wid}: missing timing phase {req_phase}")

        resources = row.get("resources", {})
        for req_res in controls["requiredResourceMetrics"]:
            if not isinstance(resources.get(req_res), (int, float)):
                failures.append(f"{wid}: missing resource metric {req_res}")
        if int(resources.get("failOrSkipCount", 1)) != 0:
            failures.append(f"{wid}: non-zero failOrSkipCount")

        dawn_p50 = float(row.get("dawnP50Ms", 0.0))
        doe_p50 = float(row.get("doeP50Ms", 0.0))
        dawn_p95 = float(row.get("dawnP95Ms", 0.0))
        doe_p95 = float(row.get("doeP95Ms", 0.0))
        if doe_p50 <= 0 or doe_p95 <= 0 or dawn_p50 <= 0 or dawn_p95 <= 0:
            failures.append(f"{wid}: non-positive p50/p95 timings")
            continue
        ratio_p50 = dawn_p50 / doe_p50
        ratio_p95 = dawn_p95 / doe_p95
        if ratio_p50 < float(wspec["minimumWinRatioP50"]):
            failures.append(f"{wid}: p50 win ratio {ratio_p50:.3f} < {wspec['minimumWinRatioP50']}")
        if ratio_p95 < float(wspec["minimumWinRatioP95"]):
            failures.append(f"{wid}: p95 win ratio {ratio_p95:.3f} < {wspec['minimumWinRatioP95']}")
        if (ratio_p50 >= suspicious_ratio or ratio_p95 >= suspicious_ratio) and not row.get("fairnessAuditPassed"):
            failures.append(
                f"{wid}: speedup >= {suspicious_ratio}x requires fairnessAuditPassed"
            )

    tail_reports = {t.get("obligationId"): t for t in receipt.get("tailObligations", [])}
    for obligation in backend_spec.get("tailRegressionObligations", []):
        oid = obligation["obligationId"]
        treport = tail_reports.get(oid)
        if treport is None:
            failures.append(f"{backend_id}: missing tail obligation report {oid}")
            continue
        if treport.get("retainedAdverseRun") is not True:
            failures.append(f"{oid}: adverse run must be retained")
        if treport.get("usedMeanToHideTail") is True:
            failures.append(f"{oid}: mean must not hide p95/p99 tail regression")
        for pct_key in obligation["requiredPercentiles"]:
            if not isinstance(treport.get(pct_key), (int, float)):
                failures.append(f"{oid}: missing required tail metric {pct_key}")
        status = treport.get("disposition")
        if status not in ("fixed-on-current-code", "unqualified-scope-disclosed"):
            failures.append(f"{oid}: invalid disposition {status!r}")

    return failures


def evaluate_chromium_ownership_receipt(
    contract: dict[str, Any],
    receipt: dict[str, Any],
) -> list[str]:
    """Check forced-Doe Chromium page ownership before 3-way browser benchmarks.

    Parameters
    ----------
    contract : dict[str, Any]
        Parsed ``webgpu-graphics-proof-campaigns.json`` object.
    receipt : dict[str, Any]
        Forced-Doe Chromium ownership and negative-control receipt.

    Returns
    -------
    list[str]
        List of ownership blockers; empty when ready for 3-way comparison.
    """
    failures: list[str] = []
    gate = contract["chromiumOwnershipGate"]
    traced = set(receipt.get("doeOwnedLifecycleStages", []))
    missing = [s for s in gate["requiredLifecycleStages"] if s not in traced]
    if missing:
        failures.append(f"missing Doe-owned page lifecycle stages: {missing}")
    if receipt.get("disabledDoeFailedVisibly") is not True:
        failures.append("disabled-Doe negative control did not fail visibly")
    if receipt.get("silentDawnFallbackOccurred") is True:
        failures.append("silent fallback to Dawn occurred in forced-Doe Chromium")
    if receipt.get("sandboxEnabled") is not True:
        failures.append("Chromium sandbox must remain enabled")
    if receipt.get("maintainabilityGatesPassed") is not True:
        failures.append("maintainability gates must pass alongside page ownership")
    if receipt.get("ranThreeWayComparison") is True and failures:
        failures.append("three-way comparison ran before page ownership passed")
    return failures


def main(argv: list[str] | None = None) -> int:
    """Run CLI contract check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default="", help="Optional Fawn repository root.")
    args = parser.parse_args(argv)
    root = resolve_repo_root(args.root or None)
    errors = validate_campaign_contract(root)
    if errors:
        for err in errors:
            print(f"FAIL: {err}", file=sys.stderr)
        return 1
    print("ok: C2 WebGPU graphics, Metal tail, and Chromium ownership contract validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
