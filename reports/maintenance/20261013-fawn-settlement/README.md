# Fawn settlement timing

Component: hosted Fawn cycle. Intent: preserved. Boundary effects: the host passes
capped visible time through the existing uniform buffer to parcel gathering;
fluid simulation retains its separate stability timestep.

The previous host advanced the formation envelope using visible elapsed time,
but parcel gathering used the smaller fluid timestep. A slow frame could therefore
report a settled formation while its parcels remained displaced. The shader also
applied strength outside the exponential, making the relaxation depend on frame
size. Gathering now uses visible time and places strength inside the exponential.
No parcel population is replaced, no scene is reseeded, and click polarity remains
Magnetic attraction versus Particle repulsion.

Evidence:

- `before.json`: actual pointer input and GPU readback on the pinned preceding
  commit, including fast and slow cadence. Its `passed` field records diagnostic
  completion, not acceptable settlement. The slow cases fail the position-error
  bound used by `check-long.mjs` despite a settled envelope.
- `slow-after.json`, `fast-after.json`: matching Flow input after the repair.
- `before.png`, `after.png`: same slow-cadence recovery point.
- `long-after.json`: repeated lower-edge input and recovery through successive
  rotations in landscape and portrait, with position-error and finite-state gates.
- `continuity.json`: existing scene-boundary, no-reseed, stalled-frame, release and
  pause checks against the repaired source.
- `pointer-direction.json`: actual parcel displacement still has the requested
  opposite click directions and unchanged force magnitudes.
- `oracles.json`: existing GPU advection, formation and transfer references, run
  separately from the larger `test-fawn-cycle.js` journeys. The fixture supplies
  the new settlement-time field; numerical tolerances are unchanged.

Reproduction from the Doe root uses `FAWN_PLAYWRIGHT_MODULE` to select the installed
Playwright module and `FAWN_REPORT_DIR` for a new output directory. Run
`node reports/maintenance/20261013-fawn-settlement/check-input.mjs` or
`node reports/maintenance/20261013-fawn-settlement/check-long.mjs`; set
`FAWN_BASELINE_REF=6239aae6a` for the pre-repair source. The existing continuity
check is `node browser/chromium/scripts/test-fawn-cycle-continuity.js`.

These are controlled software WebGPU artwork diagnostics, not physical GPU,
Safari, default-workload performance, or an unlimited-duration soak qualification.
An initial input probe was stopped because it serialized the entire target field
on every frame; the retained probe batches frames between readbacks. No interrupted
probe is counted as passing evidence.
