#!/bin/bash
# Usage (from $HOME): run-baseline.sh <workload> <log-dir>
set -eu
W=doe-intel-work
env -u VK_ICD_FILENAMES PYTHONPATH=$W/src-703ba96bf python3 -m bench.tools.dropin_phase_cohort run --inputs $W/inputs \
  --provider doe=$W/bin/harness-main --provider dawn=$W/bin/harness-dawn --provider dawn-aa=$W/bin/harness-dawn-aa \
  --workload "$1" --pairs 96 --iterations 32 --out "$2/raw-$1.jsonl"
