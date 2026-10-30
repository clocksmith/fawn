#!/bin/bash
# Round-5 master: the nine frozen windows in the frozen order, using the unchanged c5 run scripts.
W=doe-work
for exp in m7r2 m2r2 combined; do for w in vector_scale_add_262k image_rgba_invert_1024 buffer_upload_readback_exact_1mb; do
  if [ -f "$W/r5/$exp/$w/valid.txt" ]; then continue; fi
  mkdir -p "$W/r5/$exp/$w"
  "$W/r5/gated-window.sh" "$W/r5/$exp/$w" "$w" "$W/c5/run-$exp-$w.sh" > "$W/r5/$exp/$w/gated.out" 2>&1
  echo "$(date -u +%FT%TZ) $exp $w rc=$? $(tail -1 $W/r5/$exp/$w/gated.out)" >> "$W/r5/master.log"
  sleep 30
done; done
echo "$(date -u +%FT%TZ) DONE" >> "$W/r5/master.log"
