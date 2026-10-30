"""Evaluate the frozen intel-host M6 barrier contract (contract-m6.json).

Usage: gate_m6.py <summary-vs-main.json> <summary-vs-dawn.json>
"""
from __future__ import annotations

import json
import sys

W = ["vector_scale_add_262k", "image_rgba_invert_1024", "buffer_upload_readback_exact_1mb"]
WARM = ["warm.upload", "warm.encode", "warm.submit", "warm.completion", "warm.map", "warm.operation", "warmP95.operation"]


def fmt(r: dict) -> str:
    return "%.3f [%.3f,%.3f]" % (r["median"], *r["ci95"])


def main() -> None:
    m = json.load(open(sys.argv[1]))
    d = json.load(open(sys.argv[2]))
    ratio = lambda w, k: m["workloads"][w]["paired"]["m6/main"]["ratio"][k]
    ok = {"noFailedProcesses": m["failedProcesses"] == 0}
    for w in W:
        aa = d["workloads"][w]["paired"]["dawn-aa/dawn"]["ratio"]["warm.operation"]
        ok[f"aa.{w}"] = 0.95 <= aa["median"] <= 1.05 and aa["ci95"][0] <= 1 <= aa["ci95"][1]
        rd = d["workloads"][w]["paired"]["m6/dawn"]["ratio"]
        print("==", w)
        for k in ["complete", "warm.operation", "warmP95.operation", "warm.submit", "warm.completion", "warm.map",
                  "first.operation", "teardownNs", "cpuMs"]:
            print("   %-18s vs main %s   vs dawn %s" % (k, fmt(ratio(w, k)), fmt(rd[k])))
    image = W[1]
    ok["target"] = ratio(image, "complete")["ci95"][1] < 1.0 and ratio(image, "warm.completion")["ci95"][1] < 1.0
    ok["otherBound"] = all(ratio(w, "complete")["ci95"][1] <= 1.02 for w in (W[0], W[2]))
    violations = [(w, k, fmt(ratio(w, k))) for w in W for k in WARM if ratio(w, k)["ci95"][0] > 1.05]
    ok["phaseGuard"] = not violations
    print("phase guard violations", violations)
    print("GATES", ok, "VERDICT", "PASS" if all(ok.values()) else "FAIL")


if __name__ == "__main__":
    main()
