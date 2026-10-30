#!/bin/bash
# Usage: quiet-check.sh <command...>; run under the GPU lock. Operational pre-window
# check (not part of any contract): samples host CPU busy over 10 s; if it exceeds
# 3% of capacity the window is not started (exit 76) and the caller releases the
# lock and waits. The contract gate (window_gate.py) still judges the window itself.
pct=$(python3 - <<'PY'
import time
def s():
    f=[int(x) for x in open("/proc/stat").readline().split()[1:]]
    return f[0]+f[1]+f[2]+f[5]+f[6]+f[7], sum(f)
b0,t0=s(); time.sleep(10); b1,t1=s()
print(round(100*(b1-b0)/(t1-t0),3))
PY
)
echo "$(date -u +%FT%TZ) quiet-check cpuBusyPercent=$pct" >&2
python3 -c "import sys;sys.exit(0 if $pct <= 3.0 else 1)" || exit 76
exec "$@"
