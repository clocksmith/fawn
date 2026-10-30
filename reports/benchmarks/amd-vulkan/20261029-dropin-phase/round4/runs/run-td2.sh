#!/bin/bash
set -eu
cd doe-work/src-35018c4a3
python3 -m bench.tools.dropin_phase_cohort run --inputs doe-work/c1/inputs --provider main=doe-work/c5/diagbin/d2-1823497b5 --provider m7r=doe-work/c5/diagbin/d2-m7r --workload image_rgba_invert_1024 --pairs 16 --iterations 32 --out $1/raw.jsonl
