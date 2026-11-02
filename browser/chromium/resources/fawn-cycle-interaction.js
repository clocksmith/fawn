// Pointer accumulation and scene-weighted release. Consume once after a submitted frame.
const MAGNETIC_RELEASE = Object.freeze({ chargeS: 1.5, durationS: 0.7,
  minimumCharge: 0.2, speed: 0.21, radius: 0.65 });
function createCycleInteraction(canvas, { schedule, isPaused, weights }) {
  const pointer = {
    x: 0.5,
    y: 0.5,
    dx: 0,
    dy: 0,
    hover: false,
    down: false,
    press: 0,
    moved: false,
    id: null,
    heldS: 0,
  };
  const burst = { x: 0.5, y: 0.5, age: 0, charge: 0, strength: 0,
    radius: MAGNETIC_RELEASE.radius };
  function clear() {
    if (pointer.id !== null && canvas.hasPointerCapture(pointer.id))
      canvas.releasePointerCapture(pointer.id);
    Object.assign(pointer, {
      id: null,
      down: false,
      hover: false,
      dx: 0,
      dy: 0,
      press: 0,
      moved: false,
      heldS: 0,
    });
    burst.charge = burst.strength = 0;
  }
  function point(event) {
    const [x, y] = FawnFormation.screenPoint(event, canvas);
    if (pointer.hover || pointer.down) {
      pointer.dx += x - pointer.x;
      pointer.dy += y - pointer.y;
    }
    pointer.x = x;
    pointer.y = y;
    pointer.moved = true;
  }
  canvas.addEventListener('pointerdown', (event) => {
    if (pointer.id !== null && pointer.id !== event.pointerId) return;
    pointer.id = event.pointerId;
    point(event);
    pointer.down = true;
    pointer.hover = true;
    pointer.press = 1;
    pointer.heldS = 0;
    burst.charge = burst.strength = 0;
    canvas.setPointerCapture(event.pointerId);
    canvas.focus({ preventScroll: true });
    schedule();
  });
  canvas.addEventListener('pointermove', (event) => {
    if (pointer.id !== null && pointer.id !== event.pointerId) return;
    point(event);
    pointer.hover = true;
    schedule();
  });
  function release(event) {
    if (pointer.id !== event.pointerId) return;
    if (event.type === 'pointerup' && pointer.down && !isPaused()) {
      const weight = weights().wave;
      burst.x = pointer.x;
      burst.y = pointer.y;
      burst.age = 0;
      burst.charge = weight * (MAGNETIC_RELEASE.minimumCharge
        + (1 - MAGNETIC_RELEASE.minimumCharge) * pointer.heldS / MAGNETIC_RELEASE.chargeS);
    } else burst.charge = burst.strength = 0;
    pointer.down = false;
    pointer.heldS = 0;
    pointer.moved = true;
    pointer.press = 0;
    pointer.id = null;
    const rect = canvas.getBoundingClientRect();
    pointer.hover =
      event.type === 'pointerup' &&
      event.pointerType === 'mouse' &&
      event.clientX >= rect.left &&
      event.clientX <= rect.right &&
      event.clientY >= rect.top &&
      event.clientY <= rect.bottom;
    schedule();
  }
  canvas.addEventListener('pointerup', release);
  canvas.addEventListener('pointercancel', release);
  canvas.addEventListener('lostpointercapture', release);
  canvas.addEventListener('pointerleave', () => {
    pointer.hover = false;
    if (!pointer.down) pointer.moved = false;
  });
  return {
    pointer, burst, clear,
    advance(elapsedDt, weights) {
      if (pointer.down)
        pointer.heldS = Math.min(MAGNETIC_RELEASE.chargeS, pointer.heldS + elapsedDt * weights.wave);
      const releaseProgress = Math.min(1, burst.age / MAGNETIC_RELEASE.durationS);
      burst.strength = burst.charge * MAGNETIC_RELEASE.speed * (1 - releaseProgress) ** 2;
      burst.age += elapsedDt;
      return pointer.down || pointer.moved || burst.strength > 0;
    },
    consume() {
      pointer.dx = pointer.dy = pointer.press = 0;
      pointer.moved = false;
    },
  };
}
export { MAGNETIC_RELEASE, createCycleInteraction };
