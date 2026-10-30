"""Evaluate a frozen apple-metal phase contract against a cohort summary.

Usage: evaluate.py <contract.json> <summary.json> [--aa <summary-vs-aa-control.json>] [<gate.json> ...]
The summary comes from `dropin_phase_cohort summarize --control <control>`. When
the rules name an `aaControl` other than `control` (mechanism cohorts compare
against main), the A/A ratio is read from the --aa summary.
Rules are read from the contract's machine-readable `rules` object:
- validity: no failed process, A/A ratio window and CI containing 1 per
  workload, and every window gate valid;
- baseline contracts (`kind: baseline`): the c1 scoped warm-operation rule and
  the one-time regression bound, per workload, provider vs control;
- mechanism contracts (`kind: mechanism`): `improve` metrics need CI95 upper
  < 1.0 on every named workload, `noRegress` metrics need CI95 lower <= the
  bound on every workload.
"""
from __future__ import annotations

import json
import sys

ROWS = ["setupNs", "compileNs", "pipelineNs", "resourcesNs", "teardownNs", "first.operation", "warm.upload",
        "warm.encode", "warm.submit", "warm.completion", "warm.map", "warm.operation", "warmP95.operation",
        "complete", "processWall", "cpuMs", "maxRssMiB"]


def fmt(r: dict) -> str:
    return "%.3f [%.3f,%.3f]" % (r["median"], *r["ci95"])


def main() -> None:
    contract = json.load(open(sys.argv[1]))
    summary = json.load(open(sys.argv[2]))
    rules = contract["rules"]
    control, provider, aa = rules["control"], rules["provider"], rules["aaProvider"]
    aa_control = rules.get("aaControl", control)
    rest = sys.argv[3:]
    aa_summary = summary
    if rest[:1] == ["--aa"]:
        aa_summary, rest = json.load(open(rest[1])), rest[2:]
    elif aa_control != control:
        raise SystemExit("rules name a separate aaControl; pass --aa <summary>")
    ok = {"noFailedProcesses": summary["failedProcesses"] == 0 and aa_summary["failedProcesses"] == 0}
    for path in rest:
        ok[f"gate:{path}"] = json.load(open(path))["valid"]
    verdict = {}
    for w in rules["workloads"]:
        sec = summary["workloads"][w]
        ratio_aa = aa_summary["workloads"][w]["paired"][f"{aa}/{aa_control}"]["ratio"]["warm.operation"]
        lo, hi = rules["aaWindow"]
        ok[f"aa.{w}"] = lo <= ratio_aa["median"] <= hi and ratio_aa["ci95"][0] <= 1 <= ratio_aa["ci95"][1]
        r = sec["paired"][f"{provider}/{control}"]["ratio"]
        pm = sec["providers"]
        print(f"== {w} pairs={sec['paired'][f'{provider}/{control}']['pairs']} A/A warm.operation {fmt(ratio_aa)}")
        for k in ROWS:
            if k in r:
                print("  %-18s %s %10.4f  %s %10.4f  ratio %s  faster %d/%d" % (
                    k, provider, pm[provider]["median"][k], control, pm[control]["median"][k], fmt(r[k]),
                    r[k]["providerFasterPairs"], r[k]["n"]))
        if rules["kind"] == "baseline":
            win = (r["warm.operation"]["ci95"][1] < 1 and r["warmP95.operation"]["ci95"][1] < 1 and
                   r["cpuMs"]["median"] <= 1.05 and r["maxRssMiB"]["median"] <= 1.10)
            bound = [k for k in ("setupNs", "compileNs", "pipelineNs") if k in r and r[k]["ci95"][0] > 1.10]
            verdict[w] = {"scopedWarmOperationWin": win, "oneTimeRegressions": bound}
        else:
            improve = {k: r[k]["ci95"][1] < 1.0 for k in rules["improve"].get(w, [])}
            regress = {k: r[k]["ci95"][0] <= rules["noRegressBound"] for k in rules["noRegress"] if k in r}
            verdict[w] = {"improve": improve, "noRegress": regress,
                          "pass": all(improve.values()) and all(regress.values())}
        print("  verdict", verdict[w])
    valid = all(ok.values())
    print("VALIDITY", ok, "VALID" if valid else "INVALID")
    if rules["kind"] == "mechanism":
        print("MECHANISM", "PASS" if valid and all(v["pass"] for v in verdict.values()) else "FAIL")


if __name__ == "__main__":
    main()
