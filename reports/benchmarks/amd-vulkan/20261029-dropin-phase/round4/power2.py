import json, math, random, statistics as st
from power import ratios
base = "doe-work/c4/"
W = ["vector_scale_add_262k", "image_rgba_invert_1024", "buffer_upload_readback_exact_1mb"]
rng = random.Random(20261030)
def ci_upper(sample, resamples=400):
    meds = sorted(st.median(rng.choices(sample, k=len(sample))) for _ in range(resamples))
    return meds[int(0.975 * resamples) - 1]
def power(lr, bound, n, sims=200):
    # Empirical shape from the observed pairs, centered at the observed median (assumed true effect).
    hits = sum(1 for _ in range(sims) if math.exp(ci_upper(rng.choices(lr, k=n))) < bound)
    return hits / sims
for cand, run in [("m7r", "m7r-run2"), ("m2r", "m2r")]:
    for w in W:
        lr = ratios(base + run + "/raw-" + w + ".jsonl", cand)
        bound = 1.02 if (cand == "m7r" and w == W[2]) else 1.0
        if cand == "m7r" and w == W[2]:
            lr = [x - st.median(lr) for x in lr]   # assume true ratio exactly 1.00: conservative for the bound
        row = []
        for n in [24, 48, 72, 96, 144, 192, 240]:
            p = power(lr, bound, n); row.append((n, p))
            if p >= 0.8: break
        print(cand, w, "bound", bound, row)
