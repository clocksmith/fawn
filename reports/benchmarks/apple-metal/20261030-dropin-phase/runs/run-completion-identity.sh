# usage: run.sh <work> <name> <cand-harness> : one contract, three workloads, at most two attempts each.
set -u
W=$1; name=$2; cand=$3
cd $W/src-main
# Scheduling only (not a validity rule), as in runs/run-b3-m1r-m2r.sh.
settle() {
  ok=0; start=$(date +%s)
  while [ $ok -lt 20 ]; do
    [ $(( $(date +%s) - start )) -gt 14400 ] && return 1
    if ps -Ao %cpu= | awk '{s+=$1; if ($1>50) b=1} END {exit (b || s>=120)}'; then ok=$((ok+1)); else ok=0; fi
    sleep 1
  done
}
for wl in vector_scale_add_262k image_rgba_invert_1024 buffer_upload_readback_exact_1mb; do
  mkdir -p $W/$name/$wl
  for attempt in 1 2; do
    settle || { echo "$name $wl no settled slot"; break; }
    bash $W/tools/wait_quiet.sh 5400 || { echo "$name $wl no quiet slot"; break; }
    flock /tmp/doe-gpu-timing.lock caffeinate -i env PYTHONPATH=$W/src-main python3 $W/tools/mac_window.py --out-dir $W/$name/$wl/attempt$attempt --lock $W/timing.lock -- \
      --inputs $W/inputs --provider main=$W/bin/harness-main --provider cand=$W/bin/$cand --provider dawn=$W/bin/harness-dawn --provider dawn-aa=$W/bin/harness-dawn-aa \
      --workload $wl --pairs 96 --iterations 32 --out $W/$name/$wl/attempt$attempt-raw.jsonl > $W/$name/$wl/attempt$attempt-run.out 2>&1
    rc=$?; echo "$(date -u +%H:%M:%SZ) $name $wl attempt$attempt rc=$rc"
    [ $rc -eq 0 ] && break
  done
done
echo "$name ALL-DONE"
