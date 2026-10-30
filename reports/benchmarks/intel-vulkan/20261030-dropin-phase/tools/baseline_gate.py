"""Evaluate the frozen intel-host baseline contract (contract-baseline.json).

Usage: baseline_gate.py <summary-vs-dawn.json>
The summary comes from `dropin_phase_cohort summarize --control dawn` over the
three workload windows. Prints the phase table and the frozen rule verdicts.
"""
from __future__ import annotations

import json
import sys

W = ["vector_scale_add_262k", "image_rgba_invert_1024", "buffer_upload_readback_exact_1mb"]
ROWS = ["setupNs", "compileNs", "pipelineNs", "resourcesNs", "teardownNs", "first.operation", "warm.upload",
        "warm.encode", "warm.submit", "warm.completion", "warm.map", "warm.operation", "warmP95.operation",
        "complete", "processWall", "cpuMs", "maxRssMiB"]


def fmt(r: dict) -> str:
    return "%.3f [%.3f,%.3f]" % (r["median"], *r["ci95"])


def main() -> None:
    s = json.load(open(sys.argv[1]))
    ok = {"noFailedProcesses": s["failedProcesses"] == 0}
    for w in W:
        sec = s["workloads"][w]
        aa = sec["paired"]["dawn-aa/dawn"]["ratio"]["warm.operation"]
        ok[f"aa.{w}"] = 0.95 <= aa["median"] <= 1.05 and aa["ci95"][0] <= 1 <= aa["ci95"][1]
        r = sec["paired"]["doe/dawn"]["ratio"]
        pm = sec["providers"]
        print(f"== {w} pairs={sec['paired']['doe/dawn']['pairs']} A/A warm.operation {fmt(aa)}")
        for k in ROWS:
            if k in r:
                print("  %-18s doe %10.4f  dawn %10.4f  ratio %s  doeFaster %d/%d" % (
                    k, pm["doe"]["median"][k], pm["dawn"]["median"][k], fmt(r[k]),
                    r[k]["providerFasterPairs"], r[k]["n"]))
        win = (r["warm.operation"]["ci95"][1] < 1 and r["warmP95.operation"]["ci95"][1] < 1 and
               r["cpuMs"]["median"] <= 1.05 and r["maxRssMiB"]["median"] <= 1.10)
        bound = [k for k in ("setupNs", "compileNs", "pipelineNs") if k in r and r[k]["ci95"][0] > 1.10]
        print(f"  scopedWarmOperationWin={'pass' if win else 'fail'} oneTimeRegressions={bound}")
    print("VALIDITY", ok, "VALID" if all(ok.values()) else "INVALID")


if __name__ == "__main__":
    main()
