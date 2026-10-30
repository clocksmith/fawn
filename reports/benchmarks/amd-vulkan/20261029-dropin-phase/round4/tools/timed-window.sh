#!/bin/bash
# Usage: timed-window.sh <log-dir> <command...>; run under flock doe-gpu-timing.lock.
set -u
LOG="$1"; shift
mkdir -p "$LOG"
UNITS="reploid-qwen-08b reploid-qwen-2b reploid-qwen-27b reploid-qwen-27b-split"
contam() { { echo "== $1 $(date -u +%FT%TZ)"; pgrep -af "qwen-economics" | grep -v pgrep; pgrep -af "chrome.*--type=renderer" | grep -v pgrep | cut -c1-120; } >> "$LOG/contamination.txt"; }
restore() {
  rc=$?
  touch "$LOG/gate.stop"; [ -n "${GATE_PID:-}" ] && wait $GATE_PID
  contam end
  systemctl --user start $UNITS
  sleep 5
  for u in $UNITS; do
    if [ "$(systemctl --user is-active $u)" != active ]; then
      echo "$u not active after start; reset-failed and retry" >> "$LOG/services-recovery.txt"
      systemctl --user reset-failed $u; systemctl --user start $u
    fi
  done
  for u in $UNITS; do printf "%s %s\n" "$u" "$(systemctl --user is-active $u)"; done > "$LOG/services-after.txt"
  date -u +%FT%TZ >> "$LOG/services-after.txt"
  exit $rc
}
trap restore EXIT
trap "exit 130" INT TERM
for u in $UNITS; do printf "%s %s\n" "$u" "$(systemctl --user is-active $u)"; done > "$LOG/services-before.txt"
date -u +%FT%TZ > "$LOG/window-start.txt"
systemctl --user stop $UNITS
for u in $UNITS; do printf "%s %s\n" "$u" "$(systemctl --user is-active $u)"; done > "$LOG/services-stopped.txt"
contam start
(fuser -v /dev/dri/renderD128 /dev/dri/card* 2>&1 || true) > "$LOG/gpu-holders.txt"
rm -f "$LOG/gate.stop"
python3 doe-work/window_gate.py "$LOG" &
GATE_PID=$!
/usr/bin/time -f "%e %U %S" -o "$LOG/own-time.txt" "$@"
