#!/usr/bin/env python3
"""Summarize fresh-launch receipts: per-launch metrics, medians across launches
with percentile-bootstrap 95% CIs, and arm/baseline ratios with bootstrap CIs.

Usage: python3 -I analyze.py RUN_DIR [--baseline chrome] [--candidate fawn_doe] [--control fawn_dawn]
Writes RUN_DIR/summary.json and RUN_DIR/summary.md.
"""
from __future__ import annotations

import argparse
import json
import math
import random
import statistics
from pathlib import Path
from typing import Any, Callable

B = 10000
SEED = 20261031
PRACTICAL = 0.03  # a classified win/loss needs a CI excluding 1.0 and a >=3% point effect


def pct(values: list[float], q: float) -> float:
    """Nearest-rank percentile."""
    s = sorted(values)
    return s[max(0, math.ceil(len(s) * q) - 1)]


def boot_ci(values: list[float], stat: Callable[[list[float]], float],
            rng: random.Random) -> list[float]:
    n = len(values)
    reps = sorted(stat([values[rng.randrange(n)] for _ in range(n)]) for _ in range(B))
    return [reps[int(0.025 * B)], reps[int(0.975 * B) - 1]]


def boot_ratio(a: list[float], b: list[float],
               stat: Callable[[list[float]], float], rng: random.Random) -> list[float]:
    """CI of stat(a)/stat(b), resampling launches of each arm independently."""
    na, nb = len(a), len(b)
    reps = []
    for _ in range(B):
        sa = stat([a[rng.randrange(na)] for _ in range(na)])
        sb = stat([b[rng.randrange(nb)] for _ in range(nb)])
        reps.append(sa / sb if sb else math.inf)
    reps.sort()
    return [reps[int(0.025 * B)], reps[int(0.975 * B) - 1]]


METRICS = [
    # key, label, unit, stat-over-launches, lower_is_better
    ("cold_first_job_ms", "cold first job (nav -> first completed job)", "ms", "p50", True),
    ("cold_first_job_ms", "cold first job (nav -> first completed job)", "ms", "p95", True),
    ("launch_to_first_job_ms", "launch -> first completed job", "ms", "p50", True),
    ("completion_p50_ms", "frame/op completion p50", "ms", "p50", True),
    ("completion_p95_ms", "frame/op completion p95", "ms", "p50", True),
    ("completion_p99_ms", "frame/op completion p99", "ms", "p50", True),
    ("interval_p50_ms", "completed-frame interval p50", "ms", "p50", True),
    ("interval_p95_ms", "completed-frame interval p95", "ms", "p50", True),
    ("interval_p99_ms", "completed-frame interval p99", "ms", "p50", True),
    ("gpu_cpu_ms_per_frame", "GPU-process CPU per frame/op", "ms", "p50", True),
    ("total_cpu_ms_per_frame", "all-process CPU per frame/op", "ms", "p50", True),
    ("gpu_mem_mb", "GPU-process memory", "MB", "p50", True),
    ("gpu_gfx_mem_mb", "GPU-process graphics memory", "MB", "p50", True),
    ("total_mem_mb", "total browser memory", "MB", "p50", True),
]


def launch_metrics(r: dict[str, Any]) -> dict[str, float]:
    s = r.get("samples") or {}
    w = r.get("window") or {}
    m = r.get("memory") or {}
    c = r.get("cold") or {}
    out = {
        "cold_first_job_ms": c.get("firstJobMs"),
        "launch_to_first_job_ms": c.get("launchToFirstJobMs"),
        "gpu_cpu_ms_per_frame": w.get("gpuCpuMsPerFrame"),
        "total_cpu_ms_per_frame": w.get("totalCpuMsPerFrame"),
    }
    lat = s.get("completionLatencyMs") or []
    if lat:
        out["completion_p50_ms"] = pct(lat, 0.5)
        out["completion_p95_ms"] = pct(lat, 0.95)
        out["completion_p99_ms"] = pct(lat, 0.99)
    iv = s.get("completedFrameIntervalMs") or []
    if len(iv) >= 10:
        out["interval_p50_ms"] = pct(iv, 0.5)
        out["interval_p95_ms"] = pct(iv, 0.95)
        out["interval_p99_ms"] = pct(iv, 0.99)
    if m.get("source") == "footprint":
        out["gpu_mem_mb"] = (m.get("gpuProcessFootprintBytes") or 0) / 1e6
        out["gpu_gfx_mem_mb"] = (m.get("gpuProcessGraphicsDirtyBytes") or 0) / 1e6
        out["total_mem_mb"] = (m.get("totalFootprintBytes") or 0) / 1e6
    elif m:
        out["gpu_mem_mb"] = (m.get("gpuProcessPrivateBytes") or 0) / 1e6
        ded = m.get("gpuProcessDedicatedGpuBytes")
        sh = m.get("gpuProcessSharedGpuBytes")
        if ded is not None:
            out["gpu_gfx_mem_mb"] = ((ded or 0) + (sh or 0)) / 1e6
        out["total_mem_mb"] = (m.get("totalPrivateBytes") or 0) / 1e6
    return {k: v for k, v in out.items() if isinstance(v, (int, float)) and math.isfinite(v)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path, help="Directory containing launch receipts")
    parser.add_argument("--baseline", default="chrome", help="Baseline arm identity")
    parser.add_argument("--candidate", default="fawn_doe", help="Candidate arm identity")
    parser.add_argument("--control", default="fawn_dawn", help="Control arm identity")
    options = parser.parse_args()
    run = options.run
    if not (run / "launches").is_dir():
        parser.error(f"Missing launch receipt directory: {run / 'launches'}")
    base, cand, ctrl = options.baseline, options.candidate, options.control
    rng = random.Random(SEED)
    rows = {}
    identity = {}
    for f in sorted((run / "launches").glob("*.json")):
        if f.name.endswith(".spec.json"):
            continue
        r = json.loads(f.read_text(encoding="utf-8"))
        sp = r["spec"]
        key = (sp["page"], sp["arm"])
        rec = rows.setdefault(key, {"launches": 0, "ok": 0, "failures": [], "metrics": [], "provider": {},
                                    "adapters": set(), "visual": [], "oracle": [], "positions": [], "versions": set()})
        rec["launches"] += 1
        rec["positions"].append(sp.get("position"))
        rec["versions"].add(r.get("browserVersion"))
        if r.get("ok"):
            rec["ok"] += 1
            rec["metrics"].append(launch_metrics(r))
        else:
            rec["failures"].append({"rep": sp["rep"], "failures": r.get("failures")})
        pm = (r.get("provider") or {}).get("doeRuntimeMapped")
        rec["provider"][str(pm)] = rec["provider"].get(str(pm), 0) + 1
        ai = r.get("adapterInfo") or {}
        rec["adapters"].add(" / ".join(str(ai.get(k) or "") for k in ("vendor", "architecture", "device", "description")))
        v = (r.get("visual") or {}).get("b")
        if v:
            rec["visual"].append(v)
        if r.get("oracle"):
            rec["oracle"].append(bool(r["oracle"]["first"] and r["oracle"]["first"].get("matched")
                                      and r["oracle"]["final"] and r["oracle"]["final"].get("matched")))
        identity.setdefault(sp["arm"], {"browserPath": sp["browserPath"].split("/")[-1].split("\\")[-1],
                                         "args": [a if "library-path" not in a else a.split("=")[0] + "=<bundled>" for a in sp["args"]],
                                         "browserVersion": r.get("browserVersion")})
    if not rows:
        parser.error(f"No launch receipts found under {run / 'launches'}")
    pages = sorted({p for p, _ in rows})
    arms = sorted({a for _, a in rows})
    summary = {"baseline": base, "candidate": cand, "control": ctrl, "bootstrap": {"resamples": B, "seed": SEED,
               "ci": "percentile 95%", "unit": "launch"}, "practicalThreshold": PRACTICAL, "arms": identity, "pages": {}}
    for page in pages:
        pg = summary["pages"][page] = {"arms": {}, "comparisons": {}}
        for arm in arms:
            rec = rows.get((page, arm))
            if not rec:
                continue
            a = pg["arms"][arm] = {"launches": rec["launches"], "ok": rec["ok"], "failures": rec["failures"],
                                   "doeRuntimeMappedCounts": rec["provider"], "adapters": sorted(rec["adapters"]),
                                   "browserVersions": sorted(x for x in rec["versions"] if x),
                                   "positionCounts": {str(p): rec["positions"].count(p) for p in sorted(set(rec["positions"]))},
                                   "metrics": {}}
            if rec["oracle"]:
                a["exactOracleMatched"] = f"{sum(rec['oracle'])}/{len(rec['oracle'])}"
            if rec["visual"]:
                hashes = sorted({v["rgbaSha256"] for v in rec["visual"]})
                a["visual"] = {"distinctHashes": len(hashes), "hashes": hashes[:3],
                               "meanLuma": statistics.median(v["meanLuma"] for v in rec["visual"]),
                               "minNonModalFraction": min(v["nonModalFraction"] for v in rec["visual"])}
            for key, label, unit, st, _ in METRICS:
                vals = [m[key] for m in rec["metrics"] if key in m]
                if len(vals) < 3:
                    continue
                q = 0.5 if st == "p50" else (0.95 if st == "p95" else 0.99)
                f = (lambda xs, q=q: pct(xs, q))
                a["metrics"][f"{key}:{st}"] = {"label": label, "unit": unit, "stat": st, "n": len(vals),
                                                "value": f(vals), "ci95": boot_ci(vals, f, rng)}
        for other in (cand, ctrl):
            if other == base or (page, other) not in rows or (page, base) not in rows:
                continue
            comp = pg["comparisons"][f"{other}/{base}"] = {}
            for key, label, unit, st, lower in METRICS:
                va = [m[key] for m in rows[(page, other)]["metrics"] if key in m]
                vb = [m[key] for m in rows[(page, base)]["metrics"] if key in m]
                if len(va) < 3 or len(vb) < 3:
                    continue
                q = 0.5 if st == "p50" else (0.95 if st == "p95" else 0.99)
                f = (lambda xs, q=q: pct(xs, q))
                point = f(va) / f(vb) if f(vb) else math.inf
                lo, hi = boot_ratio(va, vb, f, rng)
                if hi < 1 - 0 and point <= 1 - PRACTICAL:
                    verdict = "win" if lower else "loss"
                elif lo > 1 and point >= 1 + PRACTICAL:
                    verdict = "loss" if lower else "win"
                else:
                    verdict = "no clear difference"
                comp[f"{key}:{st}"] = {"label": label, "ratio": point, "ci95": [lo, hi], "verdict": verdict,
                                        "candidate": f(va), "baseline": f(vb), "unit": unit}
        # Cross-arm exact pixel agreement for deterministic pages.
        hashes = {arm: pg["arms"][arm].get("visual", {}).get("hashes") for arm in pg["arms"]}
        pg["crossArmPixelHashesEqual"] = len({tuple(h or []) for h in hashes.values()}) == 1
    (run / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (run / "summary.md").write_text(render_md(summary) + "\n", encoding="utf-8")
    print(render_md(summary))
    return 0


def fmt(v: float | None) -> str:
    if v is None:
        return "-"
    if abs(v) >= 100:
        return f"{v:.0f}"
    if abs(v) >= 10:
        return f"{v:.1f}"
    return f"{v:.2f}"


def render_md(s: dict[str, Any]) -> str:
    base, cand, ctrl = s["baseline"], s["candidate"], s["control"]
    out = []
    wins, losses = [], []
    for page, pg in s["pages"].items():
        out.append(f"### {page}\n")
        arms = pg["arms"]
        status = []
        for arm, a in arms.items():
            extra = f", oracle {a['exactOracleMatched']}" if "exactOracleMatched" in a else ""
            vis = a.get("visual", {})
            status.append(f"{arm}: {a['ok']}/{a['launches']} ok, Doe mapped {a['doeRuntimeMappedCounts']}, "
                          f"adapter {'; '.join(a['adapters'])}{extra}, pixel hashes {vis.get('distinctHashes')}")
        out.append("- " + "\n- ".join(status))
        out.append(f"- identical pixel hashes across arms: {pg['crossArmPixelHashesEqual']}\n")
        out.append(f"| metric | {base} | {cand} | {ctrl} | {cand}/{base} (95% CI) | verdict | {ctrl}/{base} (95% CI) |")
        out.append("|---|---|---|---|---|---|---|")
        comp = pg["comparisons"].get(f"{cand}/{base}", {})
        compc = pg["comparisons"].get(f"{ctrl}/{base}", {})
        for mk, c in comp.items():
            def cell(arm: str) -> str:
                m = arms.get(arm, {}).get("metrics", {}).get(mk)
                return f"{fmt(m['value'])} [{fmt(m['ci95'][0])}, {fmt(m['ci95'][1])}]" if m else "-"
            cc = compc.get(mk)
            ctrl_cell = f"{cc['ratio']:.2f} [{cc['ci95'][0]:.2f}, {cc['ci95'][1]:.2f}]" if cc else "-"
            name = f"{c['label']} {mk.split(':')[1]} ({c['unit']})"
            out.append(f"| {name} | {cell(base)} | {cell(cand)} | {cell(ctrl)} | "
                       f"{c['ratio']:.2f} [{c['ci95'][0]:.2f}, {c['ci95'][1]:.2f}] | {c['verdict']} | {ctrl_cell} |")
            item = f"{page}: {name} {c['ratio']:.2f}x [{c['ci95'][0]:.2f}, {c['ci95'][1]:.2f}]"
            if c["verdict"] == "win":
                wins.append(item)
            elif c["verdict"] == "loss":
                losses.append(item)
        out.append("")
    head = ["## Wins (Fawn+Doe better than stock, CI excludes 1.0)\n"] + ([f"- {w}" for w in wins] or ["- none"])
    head += ["", "## Losses (Fawn+Doe worse than stock, CI excludes 1.0)\n"] + ([f"- {l}" for l in losses] or ["- none"])
    return "\n".join(head + ["", "## Per page\n"] + out)


if __name__ == "__main__":
    raise SystemExit(main())
