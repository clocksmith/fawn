#!/bin/bash
for exp in m7r2 m2r2 combined; do for w in vector_scale_add_262k image_rgba_invert_1024 buffer_upload_readback_exact_1mb; do
  mkdir -p doe-work/c5/$exp/$w
  doe-work/gated-window.sh doe-work/c5/$exp/$w doe-work/c5/run-$exp-$w.sh > doe-work/c5/$exp/$w/gated.out 2>&1
  echo "$(date +%T) $exp $w rc=$? $(tail -1 doe-work/c5/$exp/$w/gated.out)" >> doe-work/c5/master.log
done; done
echo DONE >> doe-work/c5/master.log
