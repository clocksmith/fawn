#!/bin/bash
# Usage: timed-window.sh <log-dir> <command...>; run under flock doe-gpu-timing.lock.
# Round-5 operational copy of the frozen c5 timed-window.sh. The owner keeps the
# four Reploid participant units stopped and disabled, so this copy only records
# their state; it never stops, starts or restarts them. The gate sampler, the
# contamination receipts, the GPU-holder receipt and the timed command are unchanged.
set -u
LOG="$1"; shift
mkdir -p "$LOG"
UNITS="reploid-qwen-08b reploid-qwen-2b reploid-qwen-27b reploid-qwen-27b-split"
units() { for u in $UNITS; do printf "%s %s %s\n" "$u" "$(systemctl --user is-active $u)" "$(systemctl --user is-enabled $u 2>&1)"; done; date -u +%FT%TZ; }
contam() { { echo "== $1 $(date -u +%FT%TZ)"; pgrep -af "qwen-economics" | grep -v pgrep; pgrep -af "chrome.*--type=renderer" | grep -v pgrep | cut -c1-120; } >> "$LOG/contamination.txt"; }
finish() {
  rc=$?
  touch "$LOG/gate.stop"; [ -n "${GATE_PID:-}" ] && wait $GATE_PID
  contam end
  units > "$LOG/services-after.txt"
  exit $rc
}
trap finish EXIT
trap "exit 130" INT TERM
units > "$LOG/services-before.txt"
date -u +%FT%TZ > "$LOG/window-start.txt"
contam start
(fuser -v /dev/dri/renderD128 /dev/dri/card* 2>&1 || true) > "$LOG/gpu-holders.txt"
rm -f "$LOG/gate.stop"
python3 doe-work/window_gate.py "$LOG" &
GATE_PID=$!
/usr/bin/time -f "%e %U %S" -o "$LOG/own-time.txt" "$@"
