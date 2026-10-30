#!/bin/bash
# Usage: gated-window.sh <log-base> <workload> <command...>
# Round-5 operational copy of the frozen c5 gated-window.sh. Removed: the systemd
# start-limit spacing (participant units stay disabled). Added: a lock-wait timeout
# retries the lock without consuming an attempt, and each attempt's full contract
# validity (gate.json, zero failed processes, Dawn A/A for this workload) is
# evaluated before deciding to rerun. The lock is held only while a window runs.
set -u
W=doe-work
BASE="$1"; W_NAME="$2"; shift 2
mkdir -p "$BASE"
for attempt in 1 2 3 4 5 6 7 8; do
  LOG="$BASE/attempt$attempt"
  while true; do
    echo "$(date -u +%FT%TZ) attempt $attempt waiting for lock" >> "$BASE/lock.txt"
    flock -E 75 -w 14400 models-volume/doe-gpu-timing.lock "$W/r5/timed-window.sh" "$LOG" "$@" "$LOG" > "$LOG.log" 2>&1
    rc=$?
    echo "$(date -u +%FT%TZ) attempt $attempt released rc=$rc" >> "$BASE/lock.txt"
    [ "$rc" = 75 ] || break
  done
  if [ -f "$LOG/raw-$W_NAME.jsonl" ]; then
    (cd "$W/src-35018c4a3" && python3 -m bench.tools.dropin_phase_cohort summarize --raw "$LOG/raw-$W_NAME.jsonl" --control dawn --out "$LOG/window-summary-vs-dawn.json" > /dev/null 2>&1)
  fi
  python3 - "$LOG" "$W_NAME" "$rc" > "$LOG/validity.json" <<'PY'
import json, sys
log, w, rc = sys.argv[1], sys.argv[2], int(sys.argv[3])
v = {"rc": rc}
try:
    g = json.load(open(f"{log}/gate.json"))
    v.update(gateValid=g["valid"], foreignGpuBusyPercent=g["foreignGpuBusyPercent"],
             foreignCpuPercentOfCapacity=g["foreignCpuPercentOfCapacity"], foreignGpuClients=g["foreignGpuClients"])
except Exception as e:
    v["gateValid"] = False; v["gateError"] = repr(e)
try:
    s = json.load(open(f"{log}/window-summary-vs-dawn.json"))
    r = s["workloads"][w]["paired"]["dawn-aa/dawn"]["ratio"]["warm.operation"]
    v["failedProcesses"] = s["failedProcesses"]
    v["aaWarmOperation"] = {"median": r["median"], "ci95": r["ci95"]}
    v["aaValid"] = 0.95 <= r["median"] <= 1.05 and r["ci95"][0] <= 1 <= r["ci95"][1]
except Exception as e:
    v["failedProcesses"] = None; v["aaValid"] = False; v["summaryError"] = repr(e)
v["valid"] = rc == 0 and v["gateValid"] is True and v["failedProcesses"] == 0 and v["aaValid"]
print(json.dumps(v, indent=1))
PY
  echo "attempt $attempt $(python3 -c "import json;v=json.load(open('$LOG/validity.json'));print('valid=%s rc=%s gpu=%s cpu=%s clients=%s failed=%s aa=%s' % (v['valid'],v['rc'],v.get('foreignGpuBusyPercent'),v.get('foreignCpuPercentOfCapacity'),v.get('foreignGpuClients'),v.get('failedProcesses'),v.get('aaWarmOperation')))")"
  if python3 -c "import json,sys;sys.exit(0 if json.load(open('$LOG/validity.json'))['valid'] else 1)"; then
    echo "attempt$attempt" > "$BASE/valid.txt"; exit 0
  fi
  sleep 60
done
exit 1
