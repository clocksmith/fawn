// Trusted CDP client. A caller displays or inspects the returned RGBA image before
// choosing a point. Only the creating session can spend the browser's receipt.
import { createHash } from 'node:crypto';

export function reviewedClicks(session) {
  const tickets = new WeakMap();

  async function capture({ x, y, width, height }) {
    if (![x, y, width, height].every(Number.isSafeInteger) || x < 0 || y < 0 || width < 1 || height < 1
        || !Number.isSafeInteger(x + width) || !Number.isSafeInteger(y + height)) {
      throw new RangeError('Select an integer rectangle in root viewport CSS pixels');
    }
    const rectangle = Object.freeze({ x, y, width, height });
    const result = await session.send('Input.captureVisualEvidence', rectangle);
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
    const review = Object.freeze({ rectangle, documentId, surfaceId, deviceScaleFactor,
      frameRevision, baselineDigest, pixelWidth, pixelHeight, rgba });
    tickets.set(review, evidenceId);
    return review;
  }

  async function click(review, { x, y }) {
    const ticket = tickets.get(review);
    if (!ticket) throw new Error('Review the region again in this session');
    const rect = review.rectangle;
    if (![x, y].every(Number.isSafeInteger) || x < rect.x || x >= rect.x + rect.width
        || y < rect.y || y >= rect.y + rect.height) {
      throw new RangeError('Click must lie inside the reviewed rectangle');
    }
    // A disconnected or timed-out transport can lose an acknowledgment after input
    // admission. Neither retry that click nor substitute a conventional mouse event.
    tickets.delete(review);
    const result = await session.send('Input.dispatchReviewedClick', { evidenceId: ticket, x, y });
    if (result.admitted === true || (result.admitted === false && typeof result.reason === 'string' && result.reason.length)) return result;
    throw new Error('Browser returned an indeterminate input admission result');
  }

  return { capture, click };
}
