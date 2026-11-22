# Changed tiles from native compositor surfaces on macOS

Run the three-arm diagnosis on an available physical Metal adapter. It requires
Xcode, pre-authorized macOS screen capture, generated Dawn C headers, and a
directory containing the pinned native Dawn, Doe and Doe compiler dylibs. The
runner creates a disposable borderless window, filters capture by its exact
window ID and process ID, hides it after three captures, and closes it on exit.

```sh
bench/visual-diff/run-macos.sh "$dawn_include" "$native_libs" "$new_output_dir"
```

The output includes individual samples, separately measured capture times,
library/header hashes, reported hardware and driver, and the decision under
[the frozen protocol](PROTOCOL.md). Recheck saved samples with
`python3 bench/visual-diff/score-native.py "$new_output_dir/matched-native.json"`.
The changed fixture draws one opaque red rectangle onto a transparent background.
An opaque black-window control produced real one- and two-level channel changes
in other tiles during macOS capture. The exact oracle rejected that fixture.

Both native libraries run the same fused WGSL kernel on two imported BGRA
IOSurfaces. A completed ScreenCaptureKit callback and retained CVPixelBuffers
establish input availability. Each comparison ends access on both inputs, waits
on their exported Metal shared events, and maps a four-byte tile mask. The CPU
control copies every row of both inputs and compares their tiles byte for byte.
The sources stay fixed over 16 warmups and 128 measured comparisons. The test
counts application-visible transfers. Driver-internal copies are unobserved.

For a separate Chromium compositor-to-CPU measurement, use Python with
Playwright and Pillow installed and provide a Fawn/Chromium executable:

```sh
python3 bench/visual-diff/chromium_readback.py --browser "$browser_bin" --output "$cdp_receipt"
```

This control uses a disposable profile, a locally generated canvas, and CDP
`Page.captureScreenshot(fromSurface=true)`. Its clock spans PNG capture,
delivery, CPU decode and comparison with a cached reference. Runtime creation,
painting and presentation settle before that clock. The native test's clock
starts with two delivered images. A Chromium Viz measurement still requires
holding the actual producer's mailbox through a matched shared-image consumer
and returning its synchronization token on release.
