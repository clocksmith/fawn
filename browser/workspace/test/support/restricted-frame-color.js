// Invoke in a private browser fixture's isolated world under a test user's
// capture gesture. Each reader receives the exact same restricted VideoFrame.
export async function compareCapturedColorPair(makeReader, selector) {
  if (location.hostname !== '127.0.0.1') throw Error('Disposable loopback fixture required');
  const canvas = document.querySelector(selector);
  if (!(canvas instanceof HTMLCanvasElement) || canvas.width !== 512 || canvas.height !== 256) {
    throw Error('Expected the fixture’s 512 by 256 canvas');
  }
  const ctx = canvas.getContext('2d', { alpha: false });
  const paint = rgb => { ctx.fillStyle = 'rgb(' + rgb.join(',') + ')'; ctx.fillRect(0, 0, canvas.width, canvas.height); };
  const colors = [[113, 5, 0], [114, 6, 0]];
  paint(colors[0]);
  const target = await RestrictionTarget.fromElement(canvas);
  const stream = await navigator.mediaDevices.getDisplayMedia({ preferCurrentTab: true, surfaceSwitching: 'exclude',
    audio: false, video: { displaySurface: 'browser', cursor: 'never' } });
  const track = stream.getVideoTracks()[0];
  const external = makeReader('external-hint'), upload = makeReader('explicit-upload');
  let processor;
  const hash = async buffer => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', buffer)),
    value => value.toString(16).padStart(2, '0')).join('');
  try {
    if (track.getSettings().displaySurface !== 'browser') throw Error('Tab capture required');
    await track.restrictTo(target);
    await track.applyConstraints({ frameRate: { min: 5, max: 15 } });
    processor = new MediaStreamTrackProcessor({ track, maxBufferSize: 1 }).readable.getReader();
    const rows = [];
    for (const rgb of colors) {
      paint(rgb);
      await new Promise(resolve => setTimeout(resolve, 400));
      for (let i = 0; i < 2; i++) (await processor.read()).value.close();
      const { value: frame } = await processor.read();
      try {
        const width = frame.displayWidth, height = frame.displayHeight;
        if (width !== canvas.width || height !== canvas.height) throw Error('Unexpected capture dimensions');
        const planes = new Uint8Array(frame.allocationSize()); await frame.copyTo(planes);
        const pixels = new Uint8Array(width * height * 4);
        await frame.copyTo(pixels, { rect: frame.visibleRect, format: 'RGBA', colorSpace: 'srgb', layout: [{ offset: 0, stride: width * 4 }] });
        const color = [...pixels.subarray(0, 4)];
        if (!pixels.every((value, i) => value === color[i % 4])) throw Error('Expected uniform captured color');
        const imported = await external.readFrame(canvas, frame, false, 'external-texture');
        const explicit = await upload.readFrame(canvas, frame, false, 'rgba-upload');
        const tileDigest = await hash(pixels.subarray(0, 64 * 64 * 4));
        if (explicit.hashes.length !== 32 || !explicit.hashes.every(value => value === tileDigest)) throw Error('RGBA upload differs from canonical tiles');
        rows.push({ sourceRgb: rgb, nativeFormat: frame.format, nativePlaneSha256: await hash(planes), colorSpace: frame.colorSpace.toJSON(),
          width, height, mediaTimestampUs: frame.timestamp, canonicalRgba: color, canonicalSha256: await hash(pixels),
          tileCount: explicit.hashes.length, canonicalTileSha256: tileDigest, externalTileSha256: [...new Set(imported.hashes)],
          externalRehashed: imported.rehashedTiles, explicitRehashed: explicit.rehashedTiles });
      } finally { frame.close(); }
    }
    return { adapter: external.adapterInfo(), rows };
  } finally {
    track.stop(); await processor?.cancel().catch(() => {}); external.dispose(); upload.dispose();
  }
}
