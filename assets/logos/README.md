# Doe and Fawn logo archive

This directory preserves historical Doe and Fawn logo assets with dates in the
filenames. The active runtime/browser logo paths remain outside this archive:

- `assets/doe-logo.svg`
- `browser/chromium/assets/logo/source/fawn-icon-main.svg`
- `browser/chromium/assets/logo/compiled/`

The active Doe logo is `assets/doe-logo.svg`, and it should match
`assets/logos/doe/2026-06-23-doe-logo-release-evidence.svg`. The active Doe
macOS app icon uses the retained browser compiled asset at
`browser/chromium/assets/logo/compiled/macos/fawn-icon-main.icns`.

The active Fawn source is the pink/purple curled deer, selected by the owner on
October 30, 2026. It uses the same deer artwork as `assets/doe-logo.svg`, with
transparent square framing for app icons. The browser logo build also updates
`browser/chromium/resources/fawn-icon.svg`. The user-selected cleanup on
October 30, 2026 removed the selected variants from this tree. Their previous
versions remain recoverable in Git history.

## Fawn source logos

| Date | File | Source commit | Original path | Notes |
|---|---|---|---|---|
| 2026-03-01 | `fawn/source/2026-03-01-fawn-vectorized-nursery-chromium-webgpu-lane.svg` | `c223db699` | `nursery/chromium_webgpu_lane/assets/logo/source/fawn-icon-main.svg` | Large vectorized black Fawn source. |

## Fawn compiled icons

| Date | Directory | Source commit | Original path family | Notes |
|---|---|---|---|---|
| 2026-03-16 | `fawn/compiled/2026-03-16-package-webgpu/` | `fc68a8335` | `packages/webgpu/assets/fawn-icon-main-256.png` | Package 256px icon deleted with the deprecated package tree. |

## Doe logos

| Date | File | Source commit | Original path | Notes |
|---|---|---|---|---|
| 2026-03-24 | `doe/2026-03-24-doe-logo-initial.svg` | `926d34bed` | `assets/doe-logo.svg` | Initial Doe logo asset. |
| 2026-05-25 | `doe/2026-05-25-doe-logo-robustness.svg` | `f38f38cf7` | `assets/doe-logo.svg` | Robustness-era logo revision. |
| 2026-06-23 | `doe/2026-06-23-doe-logo-release-evidence.svg` | `2e7ffb664` | `assets/doe-logo.svg` | Release evidence logo revision; restored as active default on 2026-07-06. |
