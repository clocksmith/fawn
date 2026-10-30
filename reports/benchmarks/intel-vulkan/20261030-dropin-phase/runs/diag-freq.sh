#!/bin/bash
# Diagnostic (no contract, no claim): image_rgba_invert_1024, doe main / dawn / dawn-aa, 24 pairs,
# one window with the GT frequency left alone and one with min=max=RP0 (pinned), then restored.
cd "$HOME"
W=doe-intel-work; L=$W/diag-freq; mkdir -p $L
D=/sys/class/drm/card1
MIN0=$(cat $D/gt_min_freq_mhz); MAX0=$(cat $D/gt_max_freq_mhz); RP0=$(cat $D/gt_RP0_freq_mhz)
echo "original min=$MIN0 max=$MAX0 rp0=$RP0" > $L/freq.txt
restore() { echo $MIN0 | sudo -n tee $D/gt_min_freq_mhz >/dev/null; echo $MAX0 | sudo -n tee $D/gt_max_freq_mhz >/dev/null;
  echo "restored min=$(cat $D/gt_min_freq_mhz) max=$(cat $D/gt_max_freq_mhz)" >> $L/freq.txt; }
trap restore EXIT
$W/tools/gated-window.sh $L/unpinned $W/runs/run-diag-image.sh > $L/unpinned.out 2>&1
echo $RP0 | sudo -n tee $D/gt_max_freq_mhz >/dev/null; echo $RP0 | sudo -n tee $D/gt_min_freq_mhz >/dev/null
echo "pinned min=$(cat $D/gt_min_freq_mhz) max=$(cat $D/gt_max_freq_mhz)" >> $L/freq.txt
$W/tools/gated-window.sh $L/pinned $W/runs/run-diag-image.sh > $L/pinned.out 2>&1
restore; trap - EXIT
echo DIAG-DONE >> $L/freq.txt
