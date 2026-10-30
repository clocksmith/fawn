"""Diagnostic: sample gt_act_freq_mhz every 1 ms while one harness process runs image_rgba_invert_1024.
Usage (from $HOME, inside a gated window): diag-freq-trace.py <log-dir>. Alternates providers, 6 runs each."""
from __future__ import annotations
import json, os, statistics, subprocess, sys, threading, time
from pathlib import Path
sys.path.insert(0, "doe-intel-work/src-703ba96bf")
from bench.tools.dropin_phase_cohort import harness_args
out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
args = harness_args(Path("doe-intel-work/inputs"), "image_rgba_invert_1024", 32)
env = {k: v for k, v in os.environ.items() if k != "VK_ICD_FILENAMES"}
rows = []
for run in range(12):
    provider = ["doe", "dawn"][run % 2]
    binary = {"doe": "doe-intel-work/bin/harness-main", "dawn": "doe-intel-work/bin/harness-dawn"}[provider]
    samples, stop = [], threading.Event()
    def sample():
        while not stop.is_set():
            samples.append((time.monotonic(), int(Path("/sys/class/drm/card1/gt_act_freq_mhz").read_text())))
            time.sleep(0.001)
    t = threading.Thread(target=sample); t.start()
    r = subprocess.run([binary, *args], capture_output=True, text=True, env=env)
    stop.set(); t.join()
    res = json.loads(r.stdout.strip().splitlines()[-1])
    busy = [f for _, f in samples if f > 0]
    rows.append({"provider": provider, "status": res["status"], "warmCompletionMs": statistics.median(res["phases"]["completion"][1:]) / 1e6,
                 "freqSamples": len(samples), "nonzeroSamples": len(busy),
                 "freqMedianNonzeroMhz": statistics.median(busy) if busy else 0, "freqMaxMhz": max(busy) if busy else 0})
    print(json.dumps(rows[-1]))
(out / "freq-trace.jsonl").write_text("".join(json.dumps(r) + "\n" for r in rows))
