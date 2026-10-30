#!/bin/bash
set -eu
cd doe-work/src-35018c4a3
python3 -m bench.tools.dropin_phase_cohort run --inputs doe-work/c1/inputs --provider main=doe-work/c5/bin-main --provider m7r=doe-work/c5/bin-m7r --provider dawn=doe-work/c5/bin-dawn --provider dawn-aa=doe-work/c5/bin-dawn-aa --workload image_rgba_invert_1024 --pairs 96 --iterations 32 --out $1/raw-image_rgba_invert_1024.jsonl
