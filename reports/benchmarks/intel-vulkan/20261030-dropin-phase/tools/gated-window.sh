#!/bin/bash
# Usage: gated-window.sh <log-base> <command...>
# Each attempt waits until no `make -C runtime/c` runs, takes the work-root timing lock,
# runs timed-window.sh, and reruns fresh while gate.json is invalid (max 4 attempts).
set -u
BASE="$1"; shift
WORK="${DOE_INTEL_WORK:-$HOME/doe-intel-work}"
TOOLS="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$BASE"
# Safety net: whatever happens to a window, resume the paused desktop app on exit.
trap 'kill -CONT $(pgrep -x ChatGPT) 2>/dev/null' EXIT
for attempt in 1 2 3 4; do
  LOG="$BASE/attempt$attempt"
  waited=0
  while pgrep -f "^make -C runtime/c" >/dev/null; do sleep 15; waited=$((waited+15)); [ $waited -ge 7200 ] && { echo "attempt $attempt: foreign make still running after 2h"; exit 2; }; done
  sleep 20
  flock -w 3600 "$WORK/timing.lock" "$TOOLS/timed-window.sh" "$LOG" "$@" "$LOG" > "$BASE/attempt$attempt.log" 2>&1
  rc=$?
  valid=$(python3 -c "import json;print(json.load(open('$LOG/gate.json'))['valid'])" 2>/dev/null)
  echo "attempt $attempt rc=$rc gateValid=$valid $(python3 -c "import json;g=json.load(open('$LOG/gate.json'));print(g['foreignGpuBusyPercent'],g['foreignCpuPercentOfCapacity'],g['foreignRuntimeCMakeSeen'],g['foreignGpuClients'])" 2>/dev/null)"
  if [ "$rc" = 0 ] && [ "$valid" = True ]; then echo "attempt$attempt" > "$BASE/valid.txt"; exit 0; fi
done
exit 1
