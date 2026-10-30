"""One gated timing window on an Apple host (no root, no per-client GPU accounting).

Usage: mac_window.py --out-dir DIR --lock PATH -- <dropin_phase_cohort run arguments>

Before and after the window it records the power source, low-power mode, thermal
warnings, a 5-second idle-GPU probe (IOAccelerator "Device Utilization %") and
cumulative CPU time of every live process. The cohort run executes between the
snapshots under an exclusive lock. gate.json carries the frozen validity rule:
- power source and low-power mode unchanged, no thermal or performance warning;
- idle-GPU median at or below 25% before and after (desktop baseline ~13-14%);
- foreign CPU (processes alive at both edges of the run interval, excluding this
  tool and the cohort runner tree) at or below 20% of run-interval CPU capacity.
  The idle desktop (WindowServer driving a 120 Hz external display, browser and
  app renderers) measured about 13% before b2; Spotlight indexing bursts measured
  54-63% in b1.
  The process table is read immediately before and after the run, not around
  the idle probes (the b1 defect).
macOS exposes no per-process GPU time without root, so foreign GPU work during
the window is bounded only by the idle probes on either side. This is recorded,
not hidden.
"""
from __future__ import annotations

import argparse
import fcntl
import json
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

IDLE_GPU_LIMIT = 25.0
FOREIGN_CPU_LIMIT = 0.20


def run(argv: list[str]) -> str:
    return subprocess.run(argv, capture_output=True, text=True, check=False).stdout


def gpu_utilization() -> int | None:
    match = re.search(r'"Device Utilization %"=(\d+)', run(["ioreg", "-r", "-d", "1", "-c", "IOAccelerator"]))
    return int(match.group(1)) if match else None


def cpu_seconds(text: str) -> float:
    days, _, rest = text.partition("-") if "-" in text else ("0", "", text)
    parts = [float(p) for p in rest.split(":")]
    while len(parts) < 3:
        parts.insert(0, 0.0)
    return int(days) * 86400 + parts[0] * 3600 + parts[1] * 60 + parts[2]


def processes() -> dict[int, dict]:
    table = {}
    for line in run(["ps", "-Ao", "pid=,ppid=,time=,comm="]).splitlines():
        fields = line.split(None, 3)
        if len(fields) == 4:
            table[int(fields[0])] = {"ppid": int(fields[1]), "cpu": cpu_seconds(fields[2]), "comm": fields[3]}
    return table


def snapshot() -> dict:
    samples = []
    for _ in range(5):
        samples.append(gpu_utilization())
        time.sleep(1)
    batt = run(["pmset", "-g", "batt"]).splitlines()
    therm = run(["pmset", "-g", "therm"])
    low = re.search(r"lowpowermode\s+(\d+)", run(["pmset", "-g"]))
    return {"utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "monotonic": time.monotonic(),
            "powerSource": batt[0] if batt else "", "battery": batt[1].split("\t")[-1] if len(batt) > 1 else "",
            "lowPowerMode": low.group(1) if low else None,
            "thermalWarning": "No thermal warning level has been recorded" not in therm or
                              "No performance warning level has been recorded" not in therm,
            "thermal": therm.strip(), "idleGpuUtilization": samples, "loadAverage": os.getloadavg()}


def descendants(table: dict[int, dict], root: int) -> set[int]:
    found, frontier = {root}, [root]
    while frontier:
        parent = frontier.pop()
        for pid, row in table.items():
            if row["ppid"] == parent and pid not in found:
                found.add(pid)
                frontier.append(pid)
    return found


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--lock", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=False)
    with open(args.lock, "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        before = snapshot()
        # Process CPU times bracket only the run interval, the capacity denominator.
        before["processes"] = processes()
        child = subprocess.Popen([sys.executable, "-m", "bench.tools.dropin_phase_cohort", "run", *command],
                                 stdout=open(out / "run.out", "w"), stderr=subprocess.STDOUT)
        start = time.monotonic()
        excluded = {os.getpid(), child.pid}
        while child.poll() is None:
            excluded |= descendants(processes(), child.pid)
            time.sleep(0.5)
        wall = time.monotonic() - start
        after_processes = processes()
        after = snapshot()
        after["processes"] = after_processes
    excluded |= {pid for pid, row in after["processes"].items() if row["ppid"] == os.getpid()}
    foreign = sum(max(0.0, row["cpu"] - before["processes"][pid]["cpu"]) for pid, row in after["processes"].items()
                  if pid in before["processes"] and pid not in excluded and row["comm"] == before["processes"][pid]["comm"])
    capacity = wall * (os.cpu_count() or 1)
    top = sorted(((row["cpu"] - before["processes"][pid]["cpu"], row["comm"]) for pid, row in after["processes"].items()
                  if pid in before["processes"] and pid not in excluded), reverse=True)[:8]
    idle_before = statistics.median(v for v in before["idleGpuUtilization"] if v is not None)
    idle_after = statistics.median(v for v in after["idleGpuUtilization"] if v is not None)
    checks = {
        "runExitedZero": child.returncode == 0,
        "powerSourceUnchanged": before["powerSource"] == after["powerSource"],
        "lowPowerModeOff": before["lowPowerMode"] == "0" and after["lowPowerMode"] == "0",
        "noThermalWarning": not before["thermalWarning"] and not after["thermalWarning"],
        "idleGpuBefore": idle_before <= IDLE_GPU_LIMIT,
        "idleGpuAfter": idle_after <= IDLE_GPU_LIMIT,
        "foreignCpu": foreign <= FOREIGN_CPU_LIMIT * capacity,
    }
    for snap in (before, after):
        snap.pop("processes")
    gate = {"valid": all(checks.values()), "checks": checks, "windowWallSeconds": wall,
            "foreignCpuSeconds": foreign, "foreignCpuFraction": foreign / capacity if capacity else None,
            "topForeignCpu": top, "idleGpuMedianBefore": idle_before, "idleGpuMedianAfter": idle_after,
            "limits": {"idleGpuPercent": IDLE_GPU_LIMIT, "foreignCpuFraction": FOREIGN_CPU_LIMIT},
            "before": before, "after": after, "command": command}
    (out / "gate.json").write_text(json.dumps(gate, indent=2) + "\n")
    print(json.dumps({k: gate[k] for k in ("valid", "checks", "foreignCpuFraction", "idleGpuMedianBefore", "idleGpuMedianAfter")}))
    sys.exit(0 if gate["valid"] else 3)


if __name__ == "__main__":
    main()
