#!/bin/bash
# Usage: timed-window.sh <log-dir> <command...>; run under flock of the work-root timing lock.
# Work root is $DOE_INTEL_WORK (default ~/doe-intel-work); own binaries live in its bin/.
# The resident desktop app (/usr/lib/chatgpt/ChatGPT, about 8.5% of CPU capacity when idle)
# is paused with SIGSTOP for the window and resumed with SIGCONT by the EXIT trap, the
# analogue of the amd-host participant stop/restart. Its PIDs are recorded in paused.txt.
set -u
LOG="$1"; shift
WORK="${DOE_INTEL_WORK:-$HOME/doe-intel-work}"
mkdir -p "$LOG"
if pgrep -f "^make -C runtime/c" >/dev/null; then echo "foreign runtime/c make running; refusing window" > "$LOG/refused.txt"; exit 3; fi
contam() { { echo "== $1 $(date -u +%FT%TZ)"; pgrep -c -f "chrome|ChatGPT|chromium" ; } >> "$LOG/contamination.txt"; }
finish() {
  rc=$?
  touch "$LOG/gate.stop"; [ -n "${GATE_PID:-}" ] && wait $GATE_PID
  [ -n "${PAUSED:-}" ] && kill -CONT $PAUSED 2>/dev/null
  for p in ${PAUSED:-}; do printf "%s %s\n" "$p" "$(ps -o stat= -p $p)"; done > "$LOG/resumed.txt"
  contam end
  exit $rc
}
trap finish EXIT
trap "exit 130" INT TERM
date -u +%FT%TZ > "$LOG/window-start.txt"
contam start
PAUSED=$(for p in $(pgrep -x ChatGPT); do [ "$(readlink /proc/$p/exe)" = /usr/lib/chatgpt/ChatGPT ] && echo $p; done | tr "\n" " ")
[ -n "$PAUSED" ] && kill -STOP $PAUSED
echo "$PAUSED" > "$LOG/paused.txt"
sleep 2
(fuser -v /dev/dri/renderD128 /dev/dri/card* 2>&1 | awk 'NR>1{print $NF}' | sort | uniq -c || true) > "$LOG/gpu-holders.txt"
rm -f "$LOG/gate.stop"
python3 "$(dirname "$0")/intel_window_gate.py" "$LOG" "$WORK/bin/" &
GATE_PID=$!
/usr/bin/time -f "%e %U %S" -o "$LOG/own-time.txt" "$@"
