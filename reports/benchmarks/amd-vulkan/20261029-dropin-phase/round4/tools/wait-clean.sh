#!/bin/bash
# Usage: wait-clean.sh <max-seconds>; exits 0 once no qwen-economics job and no Chrome renderer run.
end=$(( $(date +%s) + $1 ))
while [ $(date +%s) -lt $end ]; do
  if ! pgrep -f "qwen-economics" >/dev/null && ! pgrep -f "chrome.*--type=renderer" >/dev/null; then exit 0; fi
  sleep 15
done
exit 1
