# Background names and menu alignment

Component: [Fawn Chromium integration](../../../browser/chromium/CATSCAN.md).
Intent: preserved. Boundary effects: public names, page titles and Backgrounds
menu styling on the three hosted pages.

The public names are Drift, Black Opal and Chroma. Existing resource filenames
and routes remain stable. The disclosure uses a plain text trigger with a small
chevron. Its dropdown shares the trigger's width; trigger and row labels align
to the same left edge. Rows retain a 44px minimum touch target, focus indication
and the current-page checkmark. The simulation code and shader source are
unchanged by this presentation revision.

[Navigation results](results.json) exercise each page on desktop, 390px mobile
and 320px narrow viewports. They check public names and titles, dropdown bounds,
edge/text alignment and touch targets, keyboard opening, Escape focus restoration,
outside/Settings dismissal, cross-page navigation and successful GPU startup.
The existing clock regression and actual particle cycle checks also pass.
The retained screenshots show DOM/menu layout; they are not GPU pixel or
compositor qualification. These are Chrome application diagnostics on physical
Intel graphics with emulated mobile viewports, not physical phone or forced-Doe
browser release evidence.

Reproduce from the Doe root:

```sh
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs node browser/chromium/scripts/test-fawn-demo-switcher.mjs
python3 bench/gates/catscan_gate.py
```

On this test machine the Chrome launch additionally uses physical Vulkan flags:
`--use-angle=vulkan --enable-features=Vulkan --disable-vulkan-surface`.
Artifacts default to `bench/out/fawn-demo-switcher/`; `FAWN_REPORT_DIR` changes
the destination. Source and retained artifact hashes are recorded in `custody.json`.
