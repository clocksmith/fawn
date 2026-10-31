#!/bin/bash
# Usage (from $HOME): run-window.sh <workload> <log-dir>
# Frozen providers: main (parent of the SPIR-V commit), cand (the SPIR-V commit),
# Dawn d04f88359 and a byte-identical Dawn harness copy (A/A).
set -eu
W=$HOME/doe-clamp-work
env -u VK_ICD_FILENAMES PYTHONPATH=$W/src-fac2bc042 python3 -m bench.tools.dropin_phase_cohort run --inputs $W/inputs \
  --provider main=$W/bin/harness-main --provider cand=$W/bin/harness-cand \
  --provider dawn=$W/bin/harness-dawn --provider dawn-aa=$W/bin/harness-dawn-aa \
  --workload "$1" --pairs 48 --iterations 32 --out "$2/raw-$1.jsonl"
