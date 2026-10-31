set -u
cd <repo>
# Scheduling only (not a validity rule): ps %cpu is a decaying average, so a
# fresh one-core job can pass wait_quiet.sh. Also wait for 20 consecutive 1 s
# samples in which no process exceeds 50% of a core and the total stays < 120%.
settle() {
  ok=0; start=$(date +%s)
  while [ $ok -lt 20 ]; do
    [ $(( $(date +%s) - start )) -gt 14400 ] && return 1
    if ps -Ao %cpu= | awk '{s+=$1; if ($1>50) b=1} END {exit (b || s>=120)}'; then ok=$((ok+1)); else ok=0; fi
    sleep 1
  done
}
win() { # name workload providers...
  name=$1; wl=$2; shift 2
  mkdir -p <work>/$name
  for attempt in 1 2; do
    if [ -e <work>/$name/$wl/attempt$attempt ]; then grep -q '"valid": true' <work>/$name/$wl/attempt$attempt/gate.json 2>/dev/null && return; continue; fi
    settle || { echo "$name $wl no settled slot"; return; }
    bash <repo>/reports/benchmarks/apple-metal/20261030-dropin-phase/tools/wait_quiet.sh 5400 || { echo "$name $wl no quiet slot"; return; }
    flock /tmp/doe-gpu-timing.lock caffeinate -i python3 <repo>/reports/benchmarks/apple-metal/20261030-dropin-phase/tools/mac_window.py --out-dir <work>/$name/$wl/attempt$attempt --lock <work>/timing.lock -- \
      --inputs <work>/inputs "$@" --workload $wl --pairs 96 --iterations 32 --out <work>/$name/$wl/attempt$attempt-raw.jsonl
    rc=$?; echo "$(date -u +%H:%M:%SZ) $name $wl attempt$attempt rc=$rc"
    [ $rc -eq 0 ] && return
  done
}
WL="vector_scale_add_262k image_rgba_invert_1024 buffer_upload_readback_exact_1mb"
case "$1" in
b3) for wl in $WL; do win b3 $wl --provider doe=<work>/bin/harness-doe --provider dawn=<work>/bin/harness-dawn --provider dawn-aa=<work>/bin/harness-dawn-aa; done;;
m1r) for wl in $WL; do win m1r $wl --provider main=<work>/bin/harness-doe --provider m1=<work>/bin/harness-doe-m1 --provider dawn=<work>/bin/harness-dawn --provider dawn-aa=<work>/bin/harness-dawn-aa; done;;
m2r) for wl in $WL; do win m2r $wl --provider main=<work>/bin/harness-doe --provider m2=<work>/bin/harness-doe-m2 --provider dawn=<work>/bin/harness-dawn --provider dawn-aa=<work>/bin/harness-dawn-aa; done;;
esac
