#!/bin/bash
# Usage: run-metal.sh <work-root>. Each window: wait for a quiet slot (scheduling
# only), then under the shared GPU lock run one untimed warm-up pass (1 pair, 4
# iterations, all providers) and the gated window. At most two fresh attempts.
set -u
W=$1; M=$W/src-fac2bc042
P="--provider main=$W/bin/harness-main --provider cand=$W/bin/harness-cand --provider dawn=$W/bin/harness-dawn --provider dawn-aa=$W/bin/harness-dawn-aa"
cd $M
for wl in vector_scale_add_262k image_rgba_invert_1024; do
  for attempt in 1 2; do
    bash $W/tools/wait_quiet.sh 5400 || { echo "$wl no quiet slot"; break; }
    mkdir -p $W/windows/$wl
    flock /tmp/doe-gpu-timing.lock bash -c "PYTHONPATH=$M python3 -m bench.tools.dropin_phase_cohort run --inputs $W/inputs $P --workload $wl --pairs 1 --iterations 4 --out $W/windows/$wl/warmup$attempt.jsonl > /dev/null && PYTHONPATH=$M caffeinate -i python3 $W/tools/mac_window.py --out-dir $W/windows/$wl/attempt$attempt --lock $W/timing.lock -- --inputs $W/inputs $P --workload $wl --pairs 48 --iterations 32 --out $W/windows/$wl/attempt$attempt-raw.jsonl"
    rc=$?; echo "$wl attempt$attempt rc=$rc"
    [ $rc -eq 0 ] && break
  done
done
echo ALL-DONE
