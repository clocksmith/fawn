set -u
cd <repo>
win() { # name workload providers...
  name=$1; wl=$2; shift 2
  mkdir -p <work>/$name
  for attempt in 1 2; do
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
