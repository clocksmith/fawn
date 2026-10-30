#!/bin/bash
set -eu
cd doe-work/src-35018c4a3
for w in vector_scale_add_262k image_rgba_invert_1024 buffer_upload_readback_exact_1mb; do
  python3 -m bench.tools.dropin_phase_cohort run --inputs doe-work/c1/inputs --provider main=doe-work/c5/bin-main --provider c-direct=doe-work/c5/bin-c-direct --provider c-stream=doe-work/c5/bin-c-stream --provider c-staging=doe-work/c5/bin-c-staging --workload $w --pairs 24 --iterations 32 --out $1/raw-$w.jsonl
done
