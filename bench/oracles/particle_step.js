// Independent scalar CPU reference for one sampled particle frame. This checks
// simulation storage only; it does not check rasterization or presentation.
// Inputs and outputs are f32; each step is rounded back to f32. The absolute
// tolerance covers normal differences in GPU f32 evaluation and inverse sqrt.
(() => {
  const limit = 0.00003;
  const clamp = (x, lo, hi) => Math.max(lo, Math.min(hi, x));
  const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
  function check(before, after, uniforms, steps, total) {
    if (before.length !== 256 * 8 || after.length !== before.length || uniforms.length !== 8 ||
        !Number.isInteger(steps) || steps < 1 || total < 256 || !Array.from(uniforms).every(Number.isFinite)) {
      throw new Error('invalid particle oracle input');
    }
    const [dt, , aspect, force, mx, my] = uniforms;
    if (!(aspect > 0)) throw new Error('invalid particle aspect');
    let maxError = 0, worst = null;
    for (let p = 0; p < 256; p++) {
      const b = p * 8;
      let [x, y, vx, vy, r, g, blue, alpha] = before.subarray(b, b + 8);
      for (let s = 0; s < steps; s++) {
        const rad = Math.hypot(x / aspect, y);
        const ol = Math.sqrt(Math.max(x * x + y * y, 1e-6));
        const il = Math.sqrt(Math.max((x + 0.0001) ** 2 + y * y, 1e-6));
        const dx = mx - x, dy = my - y;
        const ml = Math.sqrt(Math.max(dx * dx + dy * dy, 1e-6));
        const md = Math.max(dx * dx + dy * dy, 0.018);
        const orbit = 0.020 + 0.034 * (1 - smooth(0.18, 1.05, rad));
        const inward = 0.006 + 0.014 * (1 - smooth(0.24, 1.08, rad));
        const toward = force * 0.0014 / md, around = force * 0.0006 / md;
        const fx = (-y / ol) * orbit - ((x + 0.0001) / il) * inward + (dx / ml) * toward - (dy / ml) * around - vx * 0.010;
        const fy = (x / ol) * orbit - (y / il) * inward + (dy / ml) * toward + (dx / ml) * around - vy * 0.010;
        let nvx = vx * 0.986 + fx * dt * 60, nvy = vy * 0.986 + fy * dt * 60;
        const speed = Math.hypot(nvx, nvy);
        if (speed > 1.75) { nvx *= 1.75 / speed; nvy *= 1.75 / speed; }
        let nx = x + nvx * dt, ny = y + nvy * dt;
        const maxX = aspect * 1.02;
        if (nx < -maxX || nx > maxX) { nx = clamp(nx, -maxX, maxX); nvx = -nvx * 0.92; }
        if (ny < -1.02 || ny > 1.02) { ny = clamp(ny, -1.02, 1.02); nvy = -nvy * 0.92; }
        x = Math.fround(nx); y = Math.fround(ny); vx = Math.fround(nvx); vy = Math.fround(nvy);
        alpha = Math.fround(clamp(0.0016 + speed * 0.0018, 0.0016, 0.0040));
      }
      const expected = [x, y, vx, vy, r, g, blue, alpha];
      for (let c = 0; c < 8; c++) {
        const actual = after[b + c], want = expected[c];
        const error = Math.abs(actual - want);
        if (!Number.isFinite(actual) || !Number.isFinite(want) || error > limit) {
          return { matched: false, particle: p < 128 ? p : total - 256 + p, component: c, actual, expected: want, tolerance: limit };
        }
        if (error > maxError) { maxError = error; worst = { particle: p < 128 ? p : total - 256 + p, component: c }; }
      }
    }
    return { matched: true, sampleParticles: 256, totalParticles: total, steps, tolerance: limit, maxError, worst,
      scope: 'first and last 128 simulation particles of one measured frame; rendering and other frames unchecked' };
  }
  globalThis.__particleCpuOracle = check;
})();
