// Trusted CDP client. A caller displays or inspects the returned RGBA image and
// names the point to click when it captures. The browser binds the node the page
// hits at that point. Only the creating session can spend the browser's receipt.
import { createHash } from 'node:crypto';

const inside = (rect, { x, y }) => [x, y].every(Number.isSafeInteger)
  && x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;

export function reviewedClicks(session) {
  const tickets = new WeakMap();

  async function capture({ x, y, width, height }, point) {
    if (![x, y, width, height].every(Number.isSafeInteger) || x < 0 || y < 0 || width < 1 || height < 1
        || !Number.isSafeInteger(x + width) || !Number.isSafeInteger(y + height)) {
      throw new RangeError('Select an integer rectangle in root viewport CSS pixels');
    }
    const rectangle = Object.freeze({ x, y, width, height });
    if (!point || !inside(rectangle, point)) throw new RangeError('Name a click point inside the reviewed rectangle');
    const target = Object.freeze({ x: point.x, y: point.y });
    const result = await session.send('Input.captureVisualEvidence',
      { ...rectangle, targetX: target.x, targetY: target.y });
    const { evidenceId, documentId, surfaceId, deviceScaleFactor, frameRevision, baselineDigest,
      pixelWidth, pixelHeight, rgba } = result;
    const size = 4 * pixelWidth * pixelHeight;
    if (![evidenceId, documentId, surfaceId].every(id => typeof id === 'string' && id.length > 0)
        || typeof frameRevision !== 'string' || !/^\d+$/.test(frameRevision)
        || !Number.isFinite(deviceScaleFactor) || deviceScaleFactor <= 0
        || ![pixelWidth, pixelHeight, size].every(n => Number.isSafeInteger(n) && n > 0)
        || typeof rgba !== 'string' || rgba.length !== 4 * Math.ceil(size / 3)) {
      throw new Error('Incomplete visual evidence from browser');
    }
    const pixels = Buffer.from(rgba, 'base64');
    if (pixels.length !== size || createHash('sha256').update(pixels).digest('hex') !== baselineDigest) {
      throw new Error('Reviewed pixels do not match the browser digest');
    }
    const review = Object.freeze({ rectangle, point: target, documentId, surfaceId, deviceScaleFactor,
      frameRevision, baselineDigest, pixelWidth, pixelHeight, rgba });
    tickets.set(review, evidenceId);
    return review;
  }

  // `admitted` means the click won the reviewed compositor revision. `delivered`
  // means the page received it on the bound document and node; an admitted click
  // whose recipient changed is cancelled, never retargeted.
  async function click(review, { x, y } = review?.point ?? {}) {
    const ticket = tickets.get(review);
    if (!ticket) throw new Error('Review the region again in this session');
    if (x !== review.point.x || y !== review.point.y) throw new RangeError('Click the reviewed point');
    // A disconnected or timed-out transport can lose an acknowledgment after input
    // admission. Neither retry that click nor substitute a conventional mouse event.
    tickets.delete(review);
    const result = await session.send('Input.dispatchReviewedClick', { evidenceId: ticket, x, y });
    const { admitted, delivered, reason } = result ?? {};
    if (typeof admitted === 'boolean' && typeof delivered === 'boolean' && (admitted || !delivered)
        && typeof reason === 'string' && reason.length) return result;
    throw new Error('Browser returned an indeterminate input admission result');
  }

  return { capture, click };
}
