#!/bin/bash
# Run from $HOME. Idle probe, baseline (3 windows), then candidates (3 x 3 windows), each gated.
cd "$HOME"
W=doe-intel-work; T=$W/tools; L=$W/timed; mkdir -p $L
log() { echo "$(date -u +%FT%TZ) $*" >> $L/master.log; }
for w in vector_scale_add_262k image_rgba_invert_1024 buffer_upload_readback_exact_1mb; do
  $T/gated-window.sh $L/baseline/$w $W/runs/run-baseline.sh $w > $L/baseline-$w.out 2>&1; log baseline $w rc=$? $(tail -1 $L/baseline-$w.out)
done
for c in m7r:m7r m2r:m2r combined:c-direct; do n=${c%%:*}; s=${c#*:}
  for w in vector_scale_add_262k image_rgba_invert_1024 buffer_upload_readback_exact_1mb; do
    $T/gated-window.sh $L/$n/$w $W/runs/run-candidate.sh $n $s $w > $L/$n-$w.out 2>&1; log $n $w rc=$? $(tail -1 $L/$n-$w.out)
  done
done
log DONE
