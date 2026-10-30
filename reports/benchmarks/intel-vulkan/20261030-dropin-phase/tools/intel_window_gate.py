"""Contamination gate sampler for one timed window on intel-host (diagnostic tooling).

Adapted from the amd-host round-4 window_gate.py for an i915 integrated GPU.
Run in the background for the whole window; stops when <log>/gate.stop exists.

Usage: intel_window_gate.py <log-dir> <own-binary-prefix>

Foreign GPU: i915 DRM fdinfo engine time (sum of drm-engine-* ns) of every
client whose executable does not start with <own-binary-prefix>, sampled every
250 ms; a client first seen after the window started counts from zero when its
process started after the window. i915 has no device busy percentage, so the
actual GT frequency (gt_act_freq_mhz) is sampled every 100 ms and recorded.
Foreign CPU: /proc/stat busy jiffies minus our command tree (/usr/bin/time
output) minus this sampler, as a share of window seconds x online CPUs.
A `make -C runtime/c` process seen at any sample invalidates the window (another
agent's check shares this host). Writes <log>/gate.json.
"""
from __future__ import annotations

import glob
import json
import os
import subprocess
import sys
import time
from pathlib import Path

LOG = Path(sys.argv[1])
OWN_PREFIX = sys.argv[2]
FREQ = sorted(glob.glob("/sys/class/drm/card*/gt_act_freq_mhz"))
HZ = os.sysconf("SC_CLK_TCK")
BOOT = time.time() - float(Path("/proc/uptime").read_text().split()[0])
GPU_LIMIT = 2.0
CPU_LIMIT = 5.0


def cpu_busy() -> int:
    f = [int(x) for x in Path("/proc/stat").read_text().splitlines()[0].split()[1:]]
    return f[0] + f[1] + f[2] + f[5] + f[6] + f[7]  # user nice system irq softirq steal


def snapshot() -> str:
    text = subprocess.run(["ps", "-eo", "pid,ppid,etime,pcpu,rss,comm", "--sort=-pcpu"], capture_output=True,
                          text=True).stdout
    return text


def foreign_make() -> bool:
    return subprocess.run(["pgrep", "-f", "^make -C runtime/c"], capture_output=True).returncode == 0


def drm_clients() -> dict:
    out = {}
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            exe = os.readlink(f"/proc/{pid}/exe")
            fds = os.listdir(f"/proc/{pid}/fdinfo")
        except OSError:
            continue
        for fd in fds:
            try:
                text = Path(f"/proc/{pid}/fdinfo/{fd}").read_text()
            except OSError:
                continue
            if "drm-engine-" not in text:
                continue
            client = None
            total = 0
            for line in text.splitlines():
                if line.startswith("drm-client-id:"):
                    client = line.split()[1]
                elif line.startswith("drm-engine-") and line.endswith("ns"):
                    total += int(line.split()[1])
            if client is not None:
                out[(pid, client)] = (total, exe)
    return out


def start_time(pid: str) -> float:
    try:
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return BOOT + int(fields[19]) / HZ
    except (OSError, IndexError, ValueError):
        return 0.0


def main() -> None:
    t0 = time.time()
    cpu0 = cpu_busy()
    (LOG / "snapshot-start.txt").write_text(snapshot())
    first, last, owner = {}, {}, {}
    for key, (ns, exe) in drm_clients().items():
        first[key] = ns
        last[key] = ns
        owner[key] = exe
    freq = []
    make_seen = foreign_make()
    next_scan = time.time()
    while not (LOG / "gate.stop").exists():
        for path in FREQ[:1]:
            try:
                freq.append(int(Path(path).read_text()))
            except (OSError, ValueError):
                pass
        if time.time() >= next_scan:
            for key, (ns, exe) in drm_clients().items():
                if key not in first:
                    first[key] = 0 if start_time(key[0]) >= t0 else ns
                    owner[key] = exe
                last[key] = ns
            make_seen = make_seen or foreign_make()
            next_scan = time.time() + 0.25
        time.sleep(0.1)
    t1 = time.time()
    cpu1 = cpu_busy()
    (LOG / "snapshot-end.txt").write_text(snapshot())
    own = (LOG / "own-time.txt").read_text().split() if (LOG / "own-time.txt").exists() else []
    own_cpu_s = float(own[-2]) + float(own[-1]) if len(own) >= 2 else 0.0
    self_ticks = sum(int(x) for x in Path("/proc/self/stat").read_text().rsplit(")", 1)[1].split()[11:13])
    window = t1 - t0
    own_keys = [k for k in last if owner[k].startswith(OWN_PREFIX)]
    foreign_keys = [k for k in last if not owner[k].startswith(OWN_PREFIX)]
    foreign_ns = sum(last[k] - first[k] for k in foreign_keys)
    per_client: dict[str, int] = {}
    for k in foreign_keys:
        if last[k] > first[k]:
            name = os.path.basename(owner[k])
            per_client[name] = per_client.get(name, 0) + last[k] - first[k]
    capacity = window * os.cpu_count()
    foreign_cpu_s = (cpu1 - cpu0) / HZ - own_cpu_s - self_ticks / HZ
    gate = {
        "windowSeconds": round(window, 3),
        "foreignGpuBusyPercent": round(100 * foreign_ns / 1e9 / window, 3),
        "foreignGpuClients": {name: round(ns / 1e6, 3) for name, ns in sorted(per_client.items())},
        "ownGpuClientsSeen": len(own_keys),
        "gtActFreqMhzMean": round(sum(freq) / len(freq), 1) if freq else None,
        "gtActFreqSamples": len(freq),
        "foreignCpuPercentOfCapacity": round(100 * foreign_cpu_s / capacity, 3),
        "ownCpuSeconds": own_cpu_s,
        "cpus": os.cpu_count(),
        "foreignRuntimeCMakeSeen": make_seen,
        "limits": {"foreignGpuBusyPercent": GPU_LIMIT, "foreignCpuPercentOfCapacity": CPU_LIMIT,
                   "foreignRuntimeCMake": False},
    }
    gate["valid"] = (gate["foreignGpuBusyPercent"] <= GPU_LIMIT and
                     gate["foreignCpuPercentOfCapacity"] <= CPU_LIMIT and not make_seen)
    (LOG / "gate.json").write_text(json.dumps(gate, indent=1) + "\n")


if __name__ == "__main__":
    main()
