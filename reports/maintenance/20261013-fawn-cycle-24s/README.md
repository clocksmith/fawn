# Fawn cycle and interaction timing

Component: Fawn Chromium integration (hosted artwork)
Intent: changed by user direction
Boundary effects: hosted cycle and shared formation controller; no shader or runtime changes

The versioned cycle policy now gives each background a hold and a crossfade;
`timing.json` records the complete rotation duration. Input before a crossfade
caps scene progress before the quiet interval. A started crossfade always
finishes. Ambient time remains independent. Formation preserves accumulated
progress during interaction, releases smoothly, waits for the quiet interval,
then resumes through the existing spring. New scenes reset formation progress
before their first frame while preserving position and spring continuity.

Acceptance evidence:

- `node browser/chromium/scripts/test-fawn-timing.js`: full rotations, early/late
  input, active crossfades, retained partial and complete formation progress,
  quiet guard, pause and smooth retargeting.
- `check-browser.mjs`, `browser.json`: actual software WebGPU parcels through
  repeated rotations, formation, persistent allocations/uploads, finite readback,
  late/mid-blend mouse input, holding, charged magnetic release, touch
  cancellation and pause/resume. Uses a small diagnostic workload, not default
  workload performance or physical-phone qualification.
- `safari-incomplete.json`: initial Safari automation did not traverse the cycle.
  `safari-comparison.json` retains the follow-up: the prior source rendered while
  visible; the changed source also rendered frames but the page became hidden.
  Hidden pages intentionally stop. This does not qualify a complete Safari
  rotation. A further automation attempt stalled and was stopped; no Safari
  success is inferred from its available WebGPU interface.

The independent parcel/transfer shader oracles are unchanged. Hosted aliases
and controls must additionally pass the existing D4DA preview smoke before
publication. The live `release.json` remains deployment authority.
