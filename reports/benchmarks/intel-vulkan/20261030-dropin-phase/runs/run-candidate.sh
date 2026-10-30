#!/bin/bash
# Usage (from $HOME): run-candidate.sh <candidate-name> <harness-suffix> <workload> <log-dir>
set -eu
W=doe-intel-work
env -u VK_ICD_FILENAMES PYTHONPATH=$W/src-703ba96bf python3 -m bench.tools.dropin_phase_cohort run --inputs $W/inputs \
  --provider main=$W/bin/harness-main --provider "$1=$W/bin/harness-$2" \
  --provider dawn=$W/bin/harness-dawn --provider dawn-aa=$W/bin/harness-dawn-aa \
  --workload "$3" --pairs 192 --iterations 32 --out "$4/raw-$3.jsonl"
