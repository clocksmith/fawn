# Magnetic release and scene quiet interval

Component: hosted Fawn cycle. Intent: changed by the user's interaction direction.
Boundary effects: the host owns charge/release and the scene-time cap; the existing
parcel shader applies the bounded radial pulse through previously unused uniform
slots. No new buffers, scheduler, or scene population are introduced.

Magnetic presses use a softer attraction multiplier. Releasing the pointer emits
an outward pulse at the collection point, with bounded charge from the duration
held. The origin stays fixed when the pointer moves afterward; the pulse fades
and ordinary formation recovery resumes. New presses replace the old pulse.
Touch cancellation, lost capture, blur, reset and pause cannot synthesize a burst.
Particle press repulsion and hover strengths retain their preceding values.

The requested timer rule is `phase <= N - M - 1` after interaction, where `N` is
the full scene duration and `M` its transition duration. Early progress continues;
late progress is capped. The configured durations and quiet interval are in
`clock.json`. The ambient elapsed clock never rewinds. If a blend is already
visible, its current value is preserved during the quiet interval and becomes
the starting point for the resumed blend. Previously prepared GPU handoffs are
not repeated. This replaces the earlier policy that advanced scenes while held.

Evidence:

- `browser.json`: actual mouse and native browser touch events, gathered parcel
  readback, outward expansion, bounded pulse expiry, fixed release origin, logo
  recovery, cancellation, renewed quiet intervals and no repeated handoff.
- `clock.json`: early and late input, input during an existing transition,
  continuous held input, monotonic ambient time, quiet interval and completion.
- `oracles.json`: existing advection, formation and transfer references with
  unchanged tolerances. Only the oracle portion of the full cycle suite ran.
- `held.png`, `burst.png`, `recovered.png`: the rendered interaction sequence.

Run the retained `check-browser.mjs`, `check-clock.mjs` and `check-oracles.mjs`
from the Doe root. Browser checks accept `FAWN_PLAYWRIGHT_MODULE` and
`FAWN_REPORT_DIR`. Source digests bind the browser and oracle receipts. Tests use
software WebGPU; these results do not establish Safari or physical-device
performance qualification.
