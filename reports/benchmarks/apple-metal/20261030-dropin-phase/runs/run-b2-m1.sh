set -u
cd <repo>
win() { # name workload providers...
  name=$1; wl=$2; shift 2
  for attempt in 1 2; do
    bash <repo>/reports/benchmarks/apple-metal/20261030-dropin-phase/tools/wait_quiet.sh 5400 || { echo "$name $wl no quiet slot"; return; }
    caffeinate -i python3 <repo>/reports/benchmarks/apple-metal/20261030-dropin-phase/tools/mac_window.py --out-dir <work>/$name/$wl/attempt$attempt --lock <work>/timing.lock --       --inputs <work>/inputs "$@" --workload $wl --pairs 96 --iterations 32 --out <work>/$name/$wl/attempt$attempt-raw.jsonl
    rc=$?; echo "$name $wl attempt$attempt rc=$rc"
    [ $rc -eq 0 ] && return
  done
}
for wl in vector_scale_add_262k image_rgba_invert_1024 buffer_upload_readback_exact_1mb; do
  mkdir -p <work>/b2/$wl
  win b2 $wl --provider doe=<work>/bin/harness-doe --provider dawn=<work>/bin/harness-dawn --provider dawn-aa=<work>/bin/harness-dawn-aa
done
for wl in vector_scale_add_262k image_rgba_invert_1024 buffer_upload_readback_exact_1mb; do
  mkdir -p <work>/m1/$wl
  win m1 $wl --provider main=<work>/bin/harness-doe --provider m1=<work>/bin/harness-doe-m1 --provider dawn=<work>/bin/harness-dawn --provider dawn-aa=<work>/bin/harness-dawn-aa
done
