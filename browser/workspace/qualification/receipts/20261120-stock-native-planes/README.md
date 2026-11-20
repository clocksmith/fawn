# Stock Chromium can recover the same canonical tile hashes

Unmodified Chromium produced its own dirty tiles from the Y, U and V planes of a permission-restricted tab capture. The stock path passed the same Blitzen stale-write refusal and fresh-write reconciliation as the other transports. The normal workflow recorded one effect, five application HTTP requests, zero planner calls and zero model spend. Its measured observations exported 27,300 I420 bytes, returned 16,384 canonical RGBA bytes, and hashed 16,384 bytes. The total host capture payload was 43,684 bytes.

For a 512 × 256 frame of 32 tiles, the measured output bytes were

| Captured change | I420 export | Canonical RGBA output | SHA-256 input | Full canonical RGBA control | Full control SHA-256 input |
|---|---:|---:|---:|---:|---:|
| New capture or replacement permission | 196,608 | 524,288 | 1,048,576 | 524,288 | 1,048,576 |
| Unchanged frame | 196,608 | 0 | 0 | 524,288 | 524,288 |
| Patch within one tile | 196,608 | 16,384 | 16,384 | 524,288 | 1,048,576 |
| Patch crossing four neighboring tiles | 196,608 | 65,536 | 65,536 | 524,288 | 1,048,576 |
| Distinct whole-frame colors on the software Vulkan capture | 196,608 | 524,288 | 1,048,576 | 524,288 | 1,048,576 |

The plane candidate and full canonical oracle consumed the same held VideoFrame. In both software configurations every resulting tile digest matched the independent full RGBA read. The full control reuses previous digest work on an unchanged frame. Its full-frame SHA plus per-tile hashes account for twice the RGBA length on the first frame and after a change. The oracle's extra reads are measured separately. Browser-internal memory traffic, conversions, CPU time, GPU readbacks and hardware speed are outside these byte counts.

The same two CSS colors mapped to identical captured pixels under CPU raster and different captured pixels under software Vulkan. Both outcomes agreed with their own full-frame oracle. Independently, the two I420 frames reconstructed from the earlier external-sampler collision differed only in U (115 versus 114). The stock plane observer re-read all 32 tiles and recovered RGBA 114/5/2/255 versus 114/5/0/255. For a single U or V change abutting a tile corner, the algorithm conservatively verified four tiles although only one canonical tile changed. The software tab check used distinct actual sharing grants for its initial and replacement captures.

## Reproduction and limits

The opt-in transport is **stock-i420**. It uses the normal isolated, permission-restricted reader; both browser adapters pass in the identity of the local browser they launched. The normal verified-outcome receipt here used the Playwright adapter. The candidate is scoped to the upstream Linux Chromium executable identified in the receipt. Its pinned frame-sink capturer selects SharedMemoryVideoFramePool for I420, reads I420 planes, and assigns Rec.709. Arbitrary WebCodecs frames can report I420 while having other storage. Other executable images and unsupported geometry or color take the full canonical route. Frames must be unscaled and unrotated with a complete even native rectangle. A grant, owner, surface or document replacement retires the closure and its plane baseline.

The stock process verifies the executable through the live browser PID. This one-time qualification reads 534,164,032 file bytes on a cache miss, separately from capture output. That check can be shared by subsequent browser launches reading the same inode and change time. The native Fawn prototype's potential advantage is avoiding the repeated 196,608-byte export and JavaScript comparison in these cases. The tests establish no latency win, and a full or newly granted frame costs more host output with the stock plane path than with a direct canonical read.

The benchmark ran on one niced CPU in a private namespace with no physical render device. The controlled software checkout used an upstream Chromium 158.0.8108.0 executable, SHA-256 01a0f7b8dd41057bb5bf3f48a598573976059e55eda36053f2173cdc907cab59. The applicable Chromium source is 4a6fb38567da21dc86f8ba468ac0ca9002f4f606. Under an isolated disposable browser/profile/display with permission to capture the test's own tab, run

    node browser/workspace/test/unit/stock-frame-planes.test.js
    BLITZEN_TEST_ELEMENT_CAPTURE=1 BLITZEN_STOCK_FRAME_BINARY="$CHROME" node browser/workspace/test/browser/stock-plane-capture.test.js
    BLITZEN_TEST_ELEMENT_CAPTURE=1 BLITZEN_REVIEW_COMPOSITOR_TRANSPORT=stock-i420 BLITZEN_TEST_CANVAS_BACKEND=cpu node browser/workspace/test/browser/canvas-review.test.js
