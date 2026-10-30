#!/bin/bash
# Usage (from $HOME): run-diag-image.sh <log-dir>; 24-pair image diagnostic, doe main / dawn / dawn-aa.
set -eu
W=doe-intel-work
env -u VK_ICD_FILENAMES PYTHONPATH=$W/src-703ba96bf python3 -m bench.tools.dropin_phase_cohort run --inputs $W/inputs \
  --provider doe=$W/bin/harness-main --provider dawn=$W/bin/harness-dawn --provider dawn-aa=$W/bin/harness-dawn-aa \
  --workload image_rgba_invert_1024 --pairs 24 --iterations 32 --out "$1/raw-image_rgba_invert_1024.jsonl"
