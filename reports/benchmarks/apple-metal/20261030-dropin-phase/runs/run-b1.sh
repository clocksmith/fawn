set -u
cd <repo>
for wl in vector_scale_add_262k image_rgba_invert_1024 buffer_upload_readback_exact_1mb; do
  caffeinate -i python3 <repo>/reports/benchmarks/apple-metal/20261030-dropin-phase/tools/mac_window.py --out-dir <work>/b1/$wl --lock <work>/timing.lock --     --inputs <work>/inputs --provider doe=<work>/bin/harness-doe --provider dawn=<work>/bin/harness-dawn --provider dawn-aa=<work>/bin/harness-dawn-aa     --workload $wl --pairs 96 --iterations 32 --out <work>/b1/raw-$wl.jsonl
  echo "$wl window rc=$?"
done
