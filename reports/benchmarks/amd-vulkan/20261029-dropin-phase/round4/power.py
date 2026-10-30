import json, math, statistics as st, sys
def ratios(path, cand):
    rows = [json.loads(l) for l in open(path)]
    by = {}
    for r in rows:
        res = r["result"]
        c = (res["compileNs"] + res["pipelineNs"] + res["resourcesNs"] + sum(res["phases"]["operation"]) + res["teardownNs"]) / 1e6
        by.setdefault(r["pair"], {})[r["provider"]] = c
    return [math.log(p[cand] / p["main"]) for p in by.values() if cand in p and "main" in p]
def need(sd, margin):
    se = margin / (1.96 + 0.8416)
    return math.ceil((1.2533 * sd / se) ** 2)
base = "doe-work/c4/"
W = ["vector_scale_add_262k", "image_rgba_invert_1024", "buffer_upload_readback_exact_1mb"]
out = {}
for cand, run in [("m7r", "m7r-run2"), ("m2r", "m2r")]:
    for w in W:
        lr = ratios(base + run + "/raw-" + w + ".jsonl", cand)
        sd = st.stdev(lr); med = math.exp(st.median(lr))
        if cand == "m7r" and w == W[2]: margin = math.log(1.02) - math.log(1.0)   # bound 1.02, assumed true 1.00
        elif cand == "m7r": margin = -math.log(med)                                 # resolve < 1.0 at observed effect
        else: margin = -math.log(med)
        out[(cand, w)] = (round(sd, 4), round(med, 4), round(margin, 4), need(sd, margin))
        print(cand, w, "sd(log ratio)=%.4f observed median=%.4f margin=%.4f -> pairs needed %d" % out[(cand, w)])
