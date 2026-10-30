#!/bin/bash
# Usage: gated-window.sh <log-base> <command...>
# Each attempt: start-limit spacing, GPU lock, timed-window.sh; rerun while gate.json is invalid (max 4).
set -u
BASE="$1"; shift
for attempt in 1 2 3 4; do
  LOG="$BASE/attempt$attempt"
  last=$(for u in reploid-qwen-08b reploid-qwen-2b reploid-qwen-27b reploid-qwen-27b-split; do date -d "$(systemctl --user show -p ActiveEnterTimestamp --value $u)" +%s; done | sort -n | tail -1)
  until [ $(date +%s) -ge $((last+330)) ]; do sleep 10; done
  flock -w 3600 models-volume/doe-gpu-timing.lock doe-work/timed-window.sh "$LOG" "$@" "$LOG" > "$LOG.log" 2>&1
  rc=$?
  valid=$(python3 -c "import json;print(json.load(open(\"$LOG/gate.json\"))[\"valid\"])" 2>/dev/null)
  echo "attempt $attempt rc=$rc gateValid=$valid $(python3 -c "import json;g=json.load(open(\"$LOG/gate.json\"));print(g[\"foreignGpuBusyPercent\"],g[\"foreignCpuPercentOfCapacity\"],g[\"foreignGpuClients\"])" 2>/dev/null) services=$(tr "\n" " " < $LOG/services-after.txt)"
  if [ "$rc" = 0 ] && [ "$valid" = True ]; then echo "attempt$attempt" > "$BASE/valid.txt"; exit 0; fi
done
exit 1
