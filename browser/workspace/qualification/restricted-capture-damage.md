# Damage metadata on an Element Capture track

The native-helper reader copies the selected frame to canonical sRGB RGBA and
compares its tiles. A real paint confined to one tile can still require a full
frame copy. The capture path in Chromium revision
`4a6fb38567da21dc86f8ba468ac0ca9002f4f606` has three relevant boundaries.

1. Viz receives aggregated damage in surface coordinates. Element Capture
   selects a subtree render pass with its own subrectangle and transform.
   `OnFrameDamaged` invalidates the entire source for every non-entire-tab
   target, including that subtree. In `MaybeCaptureFrame`, a full invalidation
   produces a full visible-frame update rectangle. I420 and NV12 alignment
   expands the rectangle on the separate partial-damage path. An idle refresh
   and a compositor update therefore have different meanings. An arbitrary
   intersection with root damage does not establish which pixels in the
   restricted subtree changed. [Viz producer](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc#709)
   [Subtree geometry](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/components/viz/service/frame_sinks/compositor_frame_sink_support.cc#1368)
2. The counter and update rectangle travel in native `media::CaptureMetadata`
   through Mojo. The counter describes consecutive producer frames and the
   rectangle is in coded coordinates. Capture version changes on source changes
   and restriction changes. Track adaptation can drop a frame, wrap a crop, or
   physically convert and scale a frame while copying metadata. Coordinates and
   color semantics must be established for the final wrapper before using a
   rectangle to clear a tile. [Media contract](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/media/base/video_frame_metadata.h#55)
   [Mojo](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/media/mojo/mojom/video_frame_metadata_mojom_traits.cc#86)
   [Track adaptation](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/third_party/blink/renderer/modules/mediastream/video_track_adapter.cc#524)
3. The track processor wraps the delivered native frame. The WebCodecs
   `VideoFrame.metadata()` projection omits capture damage and sequence. An
   isolated world gets the same projection. The Blitzen observer also consumes
   and discards a queued frame before its next sample. A rectangle against only
   the immediately previous frame cannot establish continuity with the previous
   **observed** result. [Processor](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/third_party/blink/renderer/modules/breakout_box/frame_queue_underlying_source.cc#618)
   [WebCodecs projection](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/third_party/blink/renderer/modules/webcodecs/video_frame.cc#1227)

A future private producer must attest a conservative bound for the actual
restricted render pass, including contributing surfaces, filters, color
conversion and sampling footprints. Its consumer needs the same live grant,
track and restriction version, frame identity, pixel geometry and color
semantics. It must account for every intermediate damage interval since its
last canonical baseline. Unknown intervals, source changes, resampling without
a proven mapping, and mismatched epochs require a full canonical comparison.
A revoked grant stops capture. The document, page scripts and trace events
cannot certify any of these facts.

## Run the linked stock control

In the existing disposable Element Capture test display with a controlled
current-tab chooser, run this command from `browser/workspace`:

```sh
BLITZEN_TEST_ELEMENT_CAPTURE=1 BLITZEN_TEST_COMPOSITOR_DIGEST=cpu node qualification/restricted-capture-damage.js
```

The probe runs two fixture origins on loopback. It watches `copyTo` on the
actual isolated-world frame and forwards the original call. It records exposed
metadata field names and byte counts. Checks cover refusal of a broad-only
grant, a foreign iframe repaint, a sparse change in the selected canvas,
dispatch against stale evidence, and revocation. No probe value participates
in a permission or pixel decision.

On the matched stock Chromium 158.0.8108.0 snapshot (Linux revision 1720185),
the software-only CDP/native-helper control saw five I420 wrappers. All five
exposed an empty metadata dictionary. The foreign sibling changed zero selected
tiles. A 4 × 4 paint changed one of eight tiles in the 256 × 128 capture. The
helper copied 131,072 RGBA bytes and hashed 16,384. Stale dispatch and the
subsequent observation after revoking the grant were refused. The test ran in
a private network and filesystem namespace with a synthetic current-tab chooser.
These counts describe that geometry and software path. A linked modified Fawn
producer and a capture-copy performance improvement still require separate proof.
