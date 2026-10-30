"""Contamination gate sampler for one timed window (diagnostic tooling).

Run in the background for the whole window; stops when <log>/gate.stop exists.
Foreign GPU: amdgpu DRM fdinfo engine time of every client whose executable is
not under OWN_PREFIX, sampled every 250 ms; a client first seen after the window
started counts from zero when its process started after the window. Device-wide
gpu_busy_percent is sampled every 100 ms and recorded (it includes our own
work). Foreign CPU: /proc/stat busy jiffies minus our command tree
(/usr/bin/time output) minus this sampler. Writes <log>/gate.json.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from pathlib import Path

LOG = Path(sys.argv[1])
OWN_PREFIX = "doe-work/"
BUSY = Path("/sys/class/drm/card1/device/gpu_busy_percent")
HZ = os.sysconf("SC_CLK_TCK")
BOOT = time.time() - float(Path("/proc/uptime").read_text().split()[0])


def cpu_busy() -> int:
    f = [int(x) for x in Path("/proc/stat").read_text().splitlines()[0].split()[1:]]
    return f[0] + f[1] + f[2] + f[5] + f[6] + f[7]  # user nice system irq softirq steal


def snapshot() -> str:
    return subprocess.run(["ps", "-eo", "pid,ppid,etime,pcpu,rss,args", "--sort=-pcpu"], capture_output=True,
                          text=True).stdout


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
            client = engine = None
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
    busy = []
    next_scan = time.time()
    while not (LOG / "gate.stop").exists():
        try:
            busy.append(int(BUSY.read_text()))
        except (OSError, ValueError):
            pass
        if time.time() >= next_scan:
            for key, (ns, exe) in drm_clients().items():
                if key not in first:
                    first[key] = 0 if start_time(key[0]) >= t0 else ns
                    owner[key] = exe
                last[key] = ns
            next_scan = time.time() + 0.25
        time.sleep(0.1)
    t1 = time.time()
    cpu1 = cpu_busy()
    (LOG / "snapshot-end.txt").write_text(snapshot())
    own = (LOG / "own-time.txt").read_text().split() if (LOG / "own-time.txt").exists() else []
    own_cpu_s = float(own[-2]) + float(own[-1]) if len(own) >= 2 else 0.0
    self_ticks = sum(int(x) for x in Path("/proc/self/stat").read_text().rsplit(")", 1)[1].split()[11:13])
    window = t1 - t0
    foreign_ns = sum(last[k] - first[k] for k in last if not owner[k].startswith(OWN_PREFIX))
    foreign_clients = sorted({os.path.basename(owner[k]) for k in last
                              if not owner[k].startswith(OWN_PREFIX) and last[k] > first[k]})
    capacity = window * os.cpu_count()
    foreign_cpu_s = (cpu1 - cpu0) / HZ - own_cpu_s - self_ticks / HZ
    gate = {
        "windowSeconds": round(window, 3),
        "foreignGpuBusyPercent": round(100 * foreign_ns / 1e9 / window, 3),
        "foreignGpuClients": foreign_clients,
        "deviceGpuBusyPercentMean": round(sum(busy) / len(busy), 3) if busy else None,
        "deviceGpuBusySamples": len(busy),
        "foreignCpuPercentOfCapacity": round(100 * foreign_cpu_s / capacity, 3),
        "ownCpuSeconds": own_cpu_s,
        "cpus": os.cpu_count(),
        "limits": {"foreignGpuBusyPercent": 2.0, "foreignCpuPercentOfCapacity": 5.0},
    }
    gate["valid"] = gate["foreignGpuBusyPercent"] <= 2.0 and gate["foreignCpuPercentOfCapacity"] <= 5.0
    (LOG / "gate.json").write_text(json.dumps(gate, indent=1) + "\n")


if __name__ == "__main__":
    main()
