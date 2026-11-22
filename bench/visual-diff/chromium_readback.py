"""Measure real Chromium compositor readback on a disposable, owned canvas."""

from __future__ import annotations

import argparse
import base64
import hashlib
import io
import json
import math
from pathlib import Path
import tempfile
import time

from PIL import Image
from playwright.sync_api import sync_playwright

WIDTH = 512
HEIGHT = 256
TILE = 64
CHANGED_TILE = 10


def pixels(session) -> tuple[bytes, int]:
    shot = session.send("Page.captureScreenshot", {
        "format": "png", "fromSurface": True,
        "clip": {"x": 0, "y": 0, "width": WIDTH, "height": HEIGHT, "scale": 1},
    })
    data = base64.b64decode(shot["data"], validate=True)
    with Image.open(io.BytesIO(data)) as image:
        if image.size != (WIDTH, HEIGHT):
            raise RuntimeError(f"Unexpected capture dimensions {image.size}")
        return image.convert("RGBA").tobytes(), len(data)


def tile_mask(left: bytes, right: bytes) -> int:
    mask = 0
    for i in range(32):
        x, y = i % 8 * TILE, i // 8 * TILE
        for row in range(y, y + TILE):
            start = (row * WIDTH + x) * 4
            if left[start:start + TILE * 4] != right[start:start + TILE * 4]:
                mask |= 1 << i
                break
    return mask


def quantiles(values: list[float]) -> dict[str, float]:
    samples = sorted(values)
    return {f"p{q}": samples[math.ceil(q * len(samples) / 100) - 1] for q in (50, 95, 99)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    report = {"source": "Page.captureScreenshot(fromSurface=true)", "clock": "perf_counter_ns",
              "browserSha256": hashlib.sha256(args.browser.read_bytes()).hexdigest(),
              "measured": [], "copies": {"materializedRgbaBytesPerRead": WIDTH*HEIGHT*4,
              "modelLoaded": False, "driverCopyCount": None}}
    with tempfile.TemporaryDirectory(prefix="fawn-cdp-control-") as tmp, sync_playwright() as driver:
        context = driver.chromium.launch_persistent_context(
            tmp, executable_path=str(args.browser.resolve()), chromium_sandbox=True, headless=True,
            viewport={"width": WIDTH, "height": HEIGHT}, device_scale_factor=1,
            args=["--disable-background-networking", "--no-first-run", "--use-webgpu-runtime=dawn"])
        try:
            page = context.new_page()
            page.set_content('<style>*{margin:0}canvas{display:block}</style><canvas width="512" height="256"></canvas>')
            session = context.new_cdp_session(page)
            page.evaluate("""() => {window.paint = (on) => {
                const ctx=document.querySelector("canvas").getContext("2d",{alpha:false});
                ctx.fillStyle="black";ctx.fillRect(0,0,512,256);
                if(on){ctx.fillStyle="red";ctx.fillRect(136,72,16,16);}
                return new Promise(done=>requestAnimationFrame(()=>requestAnimationFrame(done)));};}""")
            page.evaluate("paint(false)")
            reference, _ = pixels(session)
            for i in range(16 + 128):
                changed = bool(i % 2)
                page.evaluate("on => paint(on)", changed)
                start = time.perf_counter_ns()
                current, png_bytes = pixels(session)
                ready = time.perf_counter_ns()
                mask = tile_mask(reference, current)
                end = time.perf_counter_ns()
                if mask != (1 << CHANGED_TILE if changed else 0):
                    raise RuntimeError(f"Unexpected mask {mask:#x} at capture {i}")
                if i >= 16:
                    report["measured"].append({"ns": end-start, "readDecodeNs": ready-start,
                                               "compareNs": end-ready, "pngBytes": png_bytes,
                                               "changed": changed, "mask": mask})
            report["version"] = session.send("Browser.getVersion")["product"]
        finally:
            context.close()
    report["elapsedNs"] = quantiles([s["ns"] for s in report["measured"]])
    args.output.write_text(json.dumps(report, indent=2)+"\n")
    print(report["elapsedNs"], flush=True)


if __name__ == "__main__":
    main()
