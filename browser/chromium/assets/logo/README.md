# Doe Chromium lane logo assets

Source and compiled brand assets are now centralized here.

The owner-selected Fawn artwork is the curled deer with the pink/purple gradient,
confirmed October 30, 2026. The source shares the artwork from `assets/doe-logo.svg`
at the repository root, with transparent square framing for native app icons.
The rounded spectrum-face drawing is rejected for active Fawn branding; its
June 28 archive is historical only.

Current asset filenames retain the historical `fawn-*` prefix because those are
the literal paths consumed by the lane scripts.

- Source SVG: `assets/logo/source/fawn-icon-main.svg`
- macOS compiled: `assets/logo/compiled/macos/fawn-icon-main.icns`
- Linux compiled preview PNGs: `assets/logo/compiled/linux/fawn-icon-main-<size>.png`
  - Generated sizes: 16, 32, 64, 128, 256, 512

Use the lane script to rebuild from source:

```bash
cd browser/chromium
./scripts/build-fawn-logo-assets.sh
```

The Chromium lane patcher reads the canonical source path and will prefer the
prebuilt macOS `.icns` when available.
The default build also copies the source to `resources/fawn-icon.svg`, keeping
the browser start page and native icon on the same artwork.
