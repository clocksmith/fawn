# Fawn device defaults

Component: Fawn preview artwork. Intent: distinct desktop and mobile defaults.
Boundary effects: Doe owns initial workload selection; D4DA checks and publishes
all public aliases. Shader instructions, scene timing and physics steps are unchanged.

The cycle selects its mobile preset for narrow viewports or a coarse primary
pointer, including landscape touch devices. Desktop uses its desktop preset.
Selection happens at startup: resizing, rotation and Reset preserve the selected
workload, including a manual Settings choice. The shared formation configuration
owns the preset values; retained standalone benchmark defaults are unchanged.

`cycle.json` binds source bytes, harness, browser and adapter. Both desktop and
portrait journeys use their actual selected defaults for complete rotations,
with checks for selected controls and allocated parcel/field buffer sizes.
Existing parcel references, formation, input-independent scene timing, pause,
hidden tabs, reset, rotation and lower-frame-rate scene timing remain checked.
These are Chrome application checks and emulated viewports, not physical-phone
or native-provider qualification.

D4DA's `npm run test:fawn:hosting` checks all public routes at the desktop default,
fresh touch-device portrait/landscape defaults, manual workload selection and
preservation across rotation and Reset. Screenshots remain outside Git.

Reproduce:

```sh
node browser/chromium/scripts/build-fawn-shaders.js --check
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs \
  FAWN_REPORT_DIR=/tmp/fawn-device-defaults \
  node browser/chromium/scripts/test-fawn-cycle.js
```

`/release.json` identifies the published source. Earlier receipts retain their
previous preset values and historical meaning.
