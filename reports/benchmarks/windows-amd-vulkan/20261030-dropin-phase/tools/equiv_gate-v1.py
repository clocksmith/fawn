"""Landed-profile equivalence gate on windows-amd-host (contract-landed.json).

equiv_gate.py <summary-vs-m7r.json> <summary-vs-dawn.json>
"""
import json, sys
m = json.load(open(sys.argv[1])); d = json.load(open(sys.argv[2]))
W = ["vector_scale_add_262k", "image_rgba_invert_1024", "buffer_upload_readback_exact_1mb"]
R = lambda w, k: m["workloads"][w]["paired"]["landed/m7r"]["ratio"][k]
A = lambda w: d["workloads"][w]["paired"]["dawn-aa/dawn"]["ratio"]["warm.operation"]
fmt = lambda r: "%.3f [%.3f,%.3f]" % (r["median"], *r["ci95"])
inside = lambda r, lo, hi: lo <= r["ci95"][0] and r["ci95"][1] <= hi
ok = {"validity": m["failedProcesses"] == 0 and all(0.95 <= A(w)["median"] <= 1.05 and A(w)["ci95"][0] <= 1 <= A(w)["ci95"][1] for w in W)}
ok["primary"] = all(inside(R(w, k), 0.98, 1.02) for w in W for k in ("complete", "warm.operation"))
ok["phases"] = all(inside(R(w, k), 0.97, 1.03) for w in W for k in ("warm.submit", "warm.upload", "warm.completion"))
for w in W:
    print("==", w, "  ".join(f"{k} {fmt(R(w, k))}" for k in ("complete", "warm.operation", "warm.submit", "warm.upload", "warm.completion")))
print("GATES", ok, "VERDICT", "PASS" if all(ok.values()) else "FAIL")
