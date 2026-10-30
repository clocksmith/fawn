#!/bin/bash
set -eu
cd doe-work/src-35018c4a3
python3 -m bench.tools.dropin_phase_cohort run --inputs doe-work/c1/inputs --provider main=doe-work/c5/bin-main --provider combined=doe-work/c5/bin-c-direct --provider dawn=doe-work/c5/bin-dawn --provider dawn-aa=doe-work/c5/bin-dawn-aa --workload buffer_upload_readback_exact_1mb --pairs 192 --iterations 32 --out $1/raw-buffer_upload_readback_exact_1mb.jsonl
