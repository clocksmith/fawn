import json, sys
mode, cand, path_main, path_dawn = sys.argv[1:5]
m = json.load(open(path_main)); d = json.load(open(path_dawn))
W = ["vector_scale_add_262k", "image_rgba_invert_1024", "buffer_upload_readback_exact_1mb"]
fmt = lambda r: "%.3f [%.3f,%.3f]" % (r["median"], *r["ci95"])
ok = {}
aa_ok = all(0.95 <= d["workloads"][w]["paired"]["dawn-aa/dawn"]["ratio"]["warm.operation"]["median"] <= 1.05 and
            d["workloads"][w]["paired"]["dawn-aa/dawn"]["ratio"]["warm.operation"]["ci95"][0] <= 1 <=
            d["workloads"][w]["paired"]["dawn-aa/dawn"]["ratio"]["warm.operation"]["ci95"][1] for w in W)
ok["validity"] = m["failedProcesses"] == 0 and aa_ok
print("failed processes", m["failedProcesses"], "A/A ok", aa_ok)
for w in W:
    r = m["workloads"][w]["paired"][cand + "/main"]["ratio"]; rd = d["workloads"][w]["paired"][cand + "/dawn"]["ratio"]
    pm = m["workloads"][w]["providers"]
    print("==", w, "complete ms main %.3f %s %.3f" % (pm["main"]["median"]["complete"], cand, pm[cand]["median"]["complete"]))
    for k in ["complete", "warm.operation", "warmP95.operation", "warm.upload", "warm.submit", "warm.completion", "warm.map", "first.operation", "resourcesNs", "teardownNs", "cpuMs", "maxRssMiB"]:
        print("   %-18s vs main %s   vs dawn %s" % (k, fmt(r[k]), fmt(rd[k])))
def guard(w, keys):
    r = m["workloads"][w]["paired"][cand + "/main"]["ratio"]
    return [(k, fmt(r[k])) for k in keys if k in r and r[k]["ci95"][0] > 1.05]
warm = ["warm.upload", "warm.encode", "warm.submit", "warm.completion", "warm.map", "warm.operation", "warmP95.operation"]
R = lambda w, k: m["workloads"][w]["paired"][cand + "/main"]["ratio"][k]
if mode == "m7r":
    ok["dispatch"] = all(R(w, "complete")["ci95"][1] < 1 and R(w, "warm.submit")["ci95"][1] < 1 for w in W[:2])
    ok["uploadReadbackBound"] = R(W[2], "complete")["ci95"][1] <= 1.02
    viol = [(w, v) for w in W for v in guard(w, warm)]
    ok["phaseGuard"] = not viol; print("phase guard violations", viol)
else:
    ok["primary"] = all(R(w, "complete")["ci95"][1] < 1 for w in W)
    allow = all(R(w, "warm.upload")["median"] <= 1.15 and R(w, "resourcesNs")["median"] <= 1.15 for w in W)
    others = ["warm.encode", "warm.submit", "warm.completion", "warm.map", "warm.operation", "first.operation", "setupNs", "compileNs", "pipelineNs", "teardownNs"]
    viol = [(w, v) for w in W for v in guard(w, others)]
    ok["statedPhaseAllowances"] = allow and not viol; print("allowance upload/resources ok", allow, "violations", viol)
    up = all(R(w, "warm.upload")["ci95"][1] <= 1.02 for w in W)
    print("SEPARATE uploadLatencyPhaseGate:", "pass" if up else "fail", [fmt(R(w, "warm.upload")) for w in W])
print("GATES", ok, "VERDICT", "PASS" if all(ok.values()) else "FAIL")
