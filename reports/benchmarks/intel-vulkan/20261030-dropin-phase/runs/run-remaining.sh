#!/bin/bash
# Run from $HOME on intel-host once the host passes the window gate again.
# Pending frozen experiments: M2R (contract-m2r.json), combined (contract-combined.json), M6-I (contract-m6.json).
# Rebuild-check first: every hashed binary and library in those contracts must match, or the contract is re-frozen.
cd "$HOME"
W=doe-intel-work; T=$W/tools; L=$W/timed-remaining; mkdir -p $L
log() { echo "$(date -u +%FT%TZ) $*" >> $L/master.log; }
for c in m2r:m2r combined:c-direct m6:m6; do n=${c%%:*}; s=${c#*:}
  for w in vector_scale_add_262k image_rgba_invert_1024 buffer_upload_readback_exact_1mb; do
    $T/gated-window.sh $L/$n/$w $W/runs/run-candidate.sh $n $s $w > $L/$n-$w.out 2>&1; log $n $w rc=$? $(tail -1 $L/$n-$w.out)
  done
done
log DONE
