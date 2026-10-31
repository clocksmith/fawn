"""AMD c-direct trade-off rule (owner decision 2026-10-30), evaluated on one cohort.

tradeoff_gate.py <candidate> <summary-vs-main.json> <summary-vs-dawn.json>
Rule text: reports/benchmarks/amd-vulkan/20261029-dropin-phase/landing-c-direct-amd.json
"""
import json, sys
cand, path_main, path_dawn = sys.argv[1:4]
m = json.load(open(path_main)); d = json.load(open(path_dawn))
W = ["vector_scale_add_262k", "image_rgba_invert_1024", "buffer_upload_readback_exact_1mb"]
R = lambda w, k: m["workloads"][w]["paired"][cand + "/main"]["ratio"][k]
D = lambda w, k: d["workloads"][w]["paired"][cand + "/dawn"]["ratio"][k]
A = lambda w: d["workloads"][w]["paired"]["dawn-aa/dawn"]["ratio"]["warm.operation"]
fmt = lambda r: "%.3f [%.3f,%.3f]" % (r["median"], *r["ci95"])
ok = {}
ok["validity"] = m["failedProcesses"] == 0 and all(0.95 <= A(w)["median"] <= 1.05 and A(w)["ci95"][0] <= 1 <= A(w)["ci95"][1] for w in W)
ok["completeVsMain"] = all(R(w, "complete")["ci95"][1] < 1 for w in W)
ok["completeVsDawn"] = all(D(w, "complete")["ci95"][1] < 1 for w in W)
ok["submitVsMain"] = all(R(w, "warm.submit")["ci95"][1] < 1 for w in W[:2])
ok["completionVsMain"] = all(R(w, "warm.completion")["ci95"][1] < 1 for w in W)
ok["acceptedTradeoff"] = all(R(w, k)["median"] <= 1.12 for w in W for k in ("teardownNs", "warm.upload"))
guard = ["warm.encode", "warm.map", "warm.operation", "warmP95.operation", "first.operation", "setupNs", "compileNs", "pipelineNs", "resourcesNs"]
viol = [(w, k, fmt(R(w, k))) for w in W for k in guard if R(w, k)["ci95"][0] > 1.05]
ok["phaseGuard"] = not viol
for w in W:
    print("==", w)
    for k in ["complete", "warm.submit", "warm.completion", "warm.upload", "teardownNs", "warm.operation"]:
        print("   %-16s vs main %s   vs dawn %s" % (k, fmt(R(w, k)), fmt(D(w, k))))
print("phase guard violations", viol)
print("GATES", ok, "VERDICT", "PASS" if all(ok.values()) else "FAIL")
