// Serialized only inside the isolated world belonging to one granted capture.
export async function stockFramePlaneEvidence(frame, width, height, tile, previous, lease, qualified) {
  const fallback = { plan: [], planes: null, lease, exportedBytes: 0 };
  const bounds = frame.visibleRect, color = frame.colorSpace;
  if (!qualified || frame.format !== 'I420' || !Number.isInteger(width) || !Number.isInteger(height)
      || width <= 0 || height <= 0 || width % 2 || height % 2 || width * height > 4 * 1024 * 1024
      || frame.codedWidth !== width || frame.codedHeight !== height || frame.displayWidth !== width || frame.displayHeight !== height
      || !bounds || bounds.x || bounds.y || bounds.width !== width || bounds.height !== height || frame.rotation || frame.flip
      || color?.primaries !== 'bt709' || color.transfer !== 'bt709' || color.matrix !== 'bt709' || color.fullRange !== false
      || !Number.isInteger(tile) || tile < 16 || tile > 256 || (tile & (tile - 1)) !== 0) return fallback;
  const cols = Math.ceil(width / tile), rows = Math.ceil(height / tile);
  if (cols * rows > 256) return fallback;
  const ySize = width * height, uvSize = ySize / 4;
  const layout = [{ offset: 0, stride: width }, { offset: ySize, stride: width / 2 }, { offset: ySize + uvSize, stride: width / 2 }];
  // Omitting format requests the original visible planes. An explicit RGB format converts.
  const options = { rect: bounds, layout };
  let planes, actual;
  try {
    if (frame.allocationSize(options) !== ySize + uvSize * 2) return fallback;
    planes = new Uint8Array(ySize + uvSize * 2);
    actual = await frame.copyTo(planes, options);
  } catch { return fallback; }
  fallback.exportedBytes = planes.length;
  if (actual.length !== 3 || actual.some((part, i) => part.offset !== layout[i].offset || part.stride !== layout[i].stride)) return fallback;
  const bound = previous?.lease === lease && previous.width === width && previous.height === height
    && previous.tile === tile && previous.planes?.length === planes.length && previous.plan[0] < Number.MAX_SAFE_INTEGER;
  const result = { planes, lease, width, height, tile, exportedBytes: planes.length, plan: [1, 0, null] };
  if (!bound) return result;
  const dirty = new Uint8Array(cols * rows);
  for (let plane = 0; plane < 3; ++plane) {
    const scale = plane === 0 ? 1 : 2, halo = plane === 0 ? 0 : 2;
    const { offset, stride } = layout[plane];
    const length = plane === 0 ? ySize : uvSize;
    for (let i = 0; i < length; i++) {
      if (planes[offset + i] === previous.planes[offset + i]) continue;
      const x = (i % stride) * scale, y = Math.floor(i / stride) * scale;
      const right = Math.floor(Math.min(width - 1, x + scale - 1 + halo) / tile);
      const bottom = Math.floor(Math.min(height - 1, y + scale - 1 + halo) / tile);
      for (let ty = Math.floor(Math.max(0, y - halo) / tile); ty <= bottom; ++ty)
        for (let tx = Math.floor(Math.max(0, x - halo) / tile); tx <= right; ++tx) dirty[ty * cols + tx] = 1;
    }
  }
  const prior = previous.plan[0], serial = dirty.includes(1) ? prior + 1 : prior;
  result.plan = [serial, prior, null];
  for (let i = 0; i < dirty.length; ++i) if (dirty[i]) result.plan.push(i);
  return result;
}
