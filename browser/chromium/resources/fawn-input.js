// Pointer input for the Fawn canvas: one captured pointer, per-frame travel, and the charge a
// held magnet releases as an expanding ring. Scenes interpret these signals their own way.
const RELEASE_POLICY = Object.freeze({
  chargeS: 1.5,
  durationS: 0.7,
  minimumCharge: 0.2,
  strength: 0.21,
  radius: 0.65,
});
const R = RELEASE_POLICY;

function createPointer(canvas, { magnetWeight, onInput }) {
  const state = { x: 0.5, y: 0.5, dx: 0, dy: 0, hover: false, down: false, press: 0, moved: false,
    id: null, heldS: 0 };
  const burst = { x: 0.5, y: 0.5, ageS: R.durationS, charge: 0, radius: 0, strength: 0 };
  const owns = (event) => state.id === null || state.id === event.pointerId;
  function move(event) {
    const [x, y] = FawnFormation.screenPoint(event, canvas);
    if (state.hover || state.down) {
      state.dx += x - state.x;
      state.dy += y - state.y;
    }
    Object.assign(state, { x, y, moved: true });
  }
  function release(event) {
    if (state.id !== event.pointerId) return;
    // Holding a magnet charges it; letting go sends that charge out as a ring.
    const weight = event.type === 'pointerup' && state.down ? magnetWeight() : 0;
    Object.assign(burst, { x: state.x, y: state.y, ageS: 0,
      charge: weight * (R.minimumCharge + (1 - R.minimumCharge) * state.heldS / R.chargeS) });
    const rect = canvas.getBoundingClientRect();
    const inside = event.clientX >= rect.left && event.clientX <= rect.right
      && event.clientY >= rect.top && event.clientY <= rect.bottom;
    Object.assign(state, { id: null, down: false, press: 0, heldS: 0, moved: true,
      hover: event.type === 'pointerup' && event.pointerType === 'mouse' && inside });
    onInput();
  }
  canvas.addEventListener('pointerdown', (event) => {
    if (!owns(event)) return;
    state.id = event.pointerId;
    move(event);
    Object.assign(state, { down: true, hover: true, press: 1, heldS: 0 });
    Object.assign(burst, { charge: 0, strength: 0 });
    canvas.setPointerCapture(event.pointerId);
    canvas.focus({ preventScroll: true });
    onInput();
  });
  canvas.addEventListener('pointermove', (event) => {
    if (!owns(event)) return;
    move(event);
    state.hover = true;
    onInput();
  });
  for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) canvas.addEventListener(type, release);
  canvas.addEventListener('pointerleave', () => {
    state.hover = false;
    if (!state.down) state.moved = false;
  });
  return {
    state,
    burst,
    get interacting() {
      return state.down || state.moved || burst.strength > 0;
    },
    advance(dt, magnetShare) {
      if (state.down) state.heldS = Math.min(R.chargeS, state.heldS + dt * magnetShare);
      const remaining = 1 - Math.min(1, burst.ageS / R.durationS);
      burst.strength = burst.charge * R.strength * remaining ** 2;
      burst.radius = R.radius * (1 - remaining ** 2);
      burst.ageS += dt;
    },
    endFrame() {
      Object.assign(state, { dx: 0, dy: 0, press: 0, moved: false });
    },
    clear() {
      if (state.id !== null && canvas.hasPointerCapture(state.id)) canvas.releasePointerCapture(state.id);
      Object.assign(state, { id: null, down: false, hover: false, dx: 0, dy: 0, press: 0, moved: false,
        heldS: 0 });
      Object.assign(burst, { charge: 0, strength: 0 });
    },
  };
}

export { RELEASE_POLICY, createPointer };
