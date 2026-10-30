set -u
cd <repo>
while pgrep -f run-b2-m1.sh > /dev/null; do sleep 30; done
for wl in vector_scale_add_262k image_rgba_invert_1024 buffer_upload_readback_exact_1mb; do
  mkdir -p <work>/m2/$wl
  for attempt in 1 2; do
    bash <repo>/reports/benchmarks/apple-metal/20261030-dropin-phase/tools/wait_quiet.sh 5400 || { echo "m2 $wl no quiet slot"; break; }
    caffeinate -i python3 <repo>/reports/benchmarks/apple-metal/20261030-dropin-phase/tools/mac_window.py --out-dir <work>/m2/$wl/attempt$attempt --lock <work>/timing.lock --       --inputs <work>/inputs --provider main=<work>/bin/harness-doe --provider m2=<work>/bin/harness-doe-m2 --provider dawn=<work>/bin/harness-dawn --provider dawn-aa=<work>/bin/harness-dawn-aa       --workload $wl --pairs 96 --iterations 32 --out <work>/m2/$wl/attempt$attempt-raw.jsonl
    rc=$?; echo "m2 $wl attempt$attempt rc=$rc"; [ $rc -eq 0 ] && break
  done
done
