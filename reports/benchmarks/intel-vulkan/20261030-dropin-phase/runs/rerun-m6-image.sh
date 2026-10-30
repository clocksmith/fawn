#!/bin/bash
# Fresh rerun of the gate-invalid M6-I image window under the unchanged contract (attempts 1-4 retained).
cd "$HOME"
W=doe-intel-work; L=$W/timed-remaining
$W/tools/gated-window.sh $L/m6/image_rgba_invert_1024-rerun $W/runs/run-candidate.sh m6 m6 image_rgba_invert_1024 > $L/m6-image-rerun.out 2>&1
echo "$(date -u +%FT%TZ) m6 image rerun rc=$? $(tail -1 $L/m6-image-rerun.out)" >> $L/master.log
echo RERUN-DONE >> $L/master.log
