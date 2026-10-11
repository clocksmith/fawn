# Fawn canvas initialization and terminal cleanup

Component: [Fawn Chromium integration](../../../browser/chromium/CATSCAN.md).
Intent: preserved. Boundary effects: standalone demo canvas sizing and terminal
resource cleanup. WGSL, simulation settings, menu, and hosting routes are unchanged.

Drift previously skipped initial canvas configuration and trail allocation when
its requested backing dimensions matched the HTML canvas defaults. Configuration
now has an explicit initialized state. Each hosted background scales both axes
by the same factor to fit the acquired device's texture limit. Drift retains its
existing pixel-ratio cap; the fluids retain their requested resolution where it
fits. Chroma's terminal error path unconfigures the canvas and destroys its device,
preserves the first failure, and rejects later resize work.

[Results](results.json) bind preceding and candidate HTML hashes to Chrome on the
AMD Vulkan adapter. They reproduce the previous initialization and oversized
texture failures and missing Chroma cleanup, then check the candidate's initial
render, landscape/portrait resolution bounds, aspect ratio, phone resize,
reset, resume, paused scheduling, partial initialization failure, and runtime
validation failure. Failure injection exercises actual WebGPU device ownership;
it does not establish recovery from every hardware fault. Viewports are emulated,
not physical phone qualification. These are browser application checks, not
forced-Doe execution or performance evidence.

The successful run retains intermittent adapter-unavailable attempts separately.
The harness retries only that admission failure with bounded page reloads, without
changing the selected provider. Earlier unsuccessful runs remain in [attempts](attempts/):
adapter availability prevented some comparisons; an overridden animation clock
blocked RAF-based harness polling; one assertion ran before ResizeObserver had
updated the backing dimensions. Polling now uses an independent timer and awaits
the observable resize. Historical source failures remain expected assertions.

Reproduce from the Doe root:

```sh
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs node browser/chromium/scripts/test-fawn-canvas-lifecycle.mjs
python3 bench/gates/catscan_gate.py
```

`FAWN_REPORT_DIR` selects the output directory. `FAWN_CHROME_ARGS` optionally
supplies a JSON array of browser arguments; the report records them and the
observed adapter. The default uses the physical Vulkan Chrome flags on this host.

The sibling D4DA package was also built and its existing hosting smoke passed:
all hosted backgrounds rendered/reset, repository-only pages returned not found,
and legacy service-worker cleanup preserved unrelated cache, local storage, and
OPFS data. The smoke used the same physical Vulkan flags instead of this host's
default headless graphics configuration. Reproduce from D4DA:

```sh
npm run build:fawn
node --input-type=module <<'JS'
import { chromium } from '@playwright/test';
const launch = chromium.launch.bind(chromium);
chromium.launch = options => launch({ ...options, args: [
  '--enable-unsafe-webgpu', '--use-angle=vulkan',
  '--enable-features=Vulkan', '--disable-vulkan-surface',
] });
await import('./tests/fawn-hosting-smoke.mjs');
JS
```

No public deployment was performed.
