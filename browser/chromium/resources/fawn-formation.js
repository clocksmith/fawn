(function attachFawnFormation(root) {
  'use strict';
  const POLICY = Object.freeze({ version: 1, delayS: 0.8, settleS: 7, size: 0.76,
    noiseSeed: 7319, dyeRate: 1.6, diffusionRate: 5, force: 0.7, warp: 0.05 });

  function configure(select) {
    const key = 'fawn.formation';
    try {
      const stored = localStorage.getItem(key);
      if (stored === 'free' || stored === 'branded') select.value = stored;
    } catch { /* Storage may be unavailable; the authored default still works. */ }
    select.addEventListener('change', () => {
      try { localStorage.setItem(key, select.value); } catch { /* In-memory setting remains usable. */ }
    });
    return select.value === 'branded';
  }

  function strength(ageS, enabled) {
    if (!enabled) return 0;
    const t = Math.min(1, Math.max(0, (ageS - POLICY.delayS) / POLICY.settleS));
    return t * t * (3 - 2 * t);
  }

  function distances(mask, grid, inside) {
    const d = new Float32Array(grid * grid);
    for (let i = 0; i < d.length; i++) d[i] = (mask[i] > 0.5) === inside ? grid * 2 : 0;
    const diagonal = Math.SQRT2;
    for (let y = 0; y < grid; y++) for (let x = 0; x < grid; x++) {
      const i = y * grid + x;
      if (x) d[i] = Math.min(d[i], d[i - 1] + 1);
      if (y) {
        d[i] = Math.min(d[i], d[i - grid] + 1);
        if (x) d[i] = Math.min(d[i], d[i - grid - 1] + diagonal);
        if (x + 1 < grid) d[i] = Math.min(d[i], d[i - grid + 1] + diagonal);
      }
    }
    for (let y = grid - 1; y >= 0; y--) for (let x = grid - 1; x >= 0; x--) {
      const i = y * grid + x;
      if (x + 1 < grid) d[i] = Math.min(d[i], d[i + 1] + 1);
      if (y + 1 < grid) {
        d[i] = Math.min(d[i], d[i + grid] + 1);
        if (x) d[i] = Math.min(d[i], d[i + grid - 1] + diagonal);
        if (x + 1 < grid) d[i] = Math.min(d[i], d[i + grid + 1] + diagonal);
      }
    }
    return d;
  }

  function field(grid, canvas, logo) {
    if (!Number.isInteger(grid) || grid < 2 || !logo?.querySelector('path')) {
      throw Error('Fawn formation requires a valid grid and logo paths');
    }
    const raster = document.createElement('canvas'); raster.width = raster.height = grid;
    const ctx = raster.getContext('2d', { willReadFrequently: true });
    if (!ctx) throw Error('Fawn formation target requires Canvas 2D');
    const box = logo.viewBox.baseVal;
    const scale = POLICY.size * Math.min(canvas.clientWidth / box.width,
      canvas.clientHeight / box.height);
    const width = box.width * scale / canvas.clientWidth * grid;
    const height = box.height * scale / canvas.clientHeight * grid;
    ctx.translate((grid - width) / 2, (grid - height) / 2);
    ctx.scale(width / box.width, height / box.height); ctx.translate(-box.x, -box.y);
    ctx.fillStyle = '#fff';
    for (const path of logo.querySelectorAll('path')) ctx.fill(new Path2D(path.getAttribute('d')));
    const pixels = ctx.getImageData(0, 0, grid, grid).data;
    const mask = new Float32Array(grid * grid);
    for (let y = 0; y < grid; y++) for (let x = 0; x < grid; x++) {
      mask[y * grid + x] = pixels[((grid - 1 - y) * grid + x) * 4 + 3] / 255;
    }
    const inner = distances(mask, grid, true), outer = distances(mask, grid, false);
    const signed = inner.map((v, i) => (v - outer[i]) / grid);
    const data = new Float32Array(grid * grid * 4);
    for (let y = 0; y < grid; y++) for (let x = 0; x < grid; x++) {
      const i = y * grid + x;
      const dx = signed[y * grid + Math.min(grid - 1, x + 1)] - signed[y * grid + Math.max(0, x - 1)];
      const dy = signed[Math.min(grid - 1, y + 1) * grid + x] - signed[Math.max(0, y - 1) * grid + x];
      const norm = Math.max(Math.hypot(dx, dy), 1e-6);
      data.set([dx / norm, dy / norm, mask[i], signed[i]], i * 4);
    }
    return data;
  }

  function seed(grid) {
    const dye = new Float32Array(grid * grid * 4), velocity = new Float32Array(dye.length);
    let random = POLICY.noiseSeed;
    for (let y = 0; y < grid; y++) for (let x = 0; x < grid; x++) {
      const i = (y * grid + x) * 4, u = x / grid, v = y / grid;
      random ^= random << 13; random ^= random >>> 17; random ^= random << 5;
      const grain = (random >>> 0) / 4294967296;
      const cloud = 0.35 + 0.3 * Math.sin(u * 21 + Math.sin(v * 13)) * Math.cos(v * 18 - u * 6);
      for (let c = 0; c < 3; c++) dye[i + c] = cloud * (0.55 + 0.45 * Math.cos(u * 9 + v * 7 + c * 2.1)) + grain * 0.055;
      dye[i + 3] = 1;
      velocity[i] = 0.09 * Math.sin(v * Math.PI * 4) * Math.sin(u * Math.PI);
      velocity[i + 1] = -0.09 * Math.sin(u * Math.PI * 4) * Math.sin(v * Math.PI);
    }
    return { dye, velocity };
  }

  // A bounded point cloud reuses the same aspect-correct silhouette for particle targets.
  function points(grid, canvas, logo) {
    const raster = field(grid, canvas, logo), result = [];
    for (let i = 0; i < grid * grid; i++) if (raster[i * 4 + 2] > 0.5) {
      result.push(((i % grid) + 0.5) / grid * 2 - 1,
        (Math.floor(i / grid) + 0.5) / grid * 2 - 1);
    }
    if (!result.length) throw Error('Fawn formation requires visible logo geometry');
    return new Float32Array(result);
  }

  const WGSL = `
fn logo_at(uv: vec2<f32>) -> vec4<f32> {
  let p = clamp(uv * u.grid.xy - vec2<f32>(0.5), vec2<f32>(0.0), u.grid.xy - vec2<f32>(1.0));
  let b = vec2<i32>(floor(p)); let f = fract(p);
  return mix(mix(logoField[idx_at(b.x, b.y)], logoField[idx_at(b.x + 1, b.y)], f.x),
    mix(logoField[idx_at(b.x, b.y + 1)], logoField[idx_at(b.x + 1, b.y + 1)], f.x), f.y);
}
fn logo_target(uv: vec2<f32>) -> vec3<f32> {
  let warp = ${POLICY.warp} * (1.0 - u.sim.w);
  let p = uv + warp * vec2<f32>(sin(uv.y * 18.0 + u.sim.y * 0.6), cos(uv.x * 15.0 - u.sim.y * 0.5));
  let field = logo_at(p);
  let softness = max(u.grid.z * 1.4, (1.0 - u.sim.w) * 0.026);
  let shape = smoothstep(-softness, softness, field.w);
  let band = 0.82 + 0.18 * sin(field.w * 110.0 + uv.y * 8.0 - u.sim.y * 0.16);
  let palette = vec3<f32>(0.52, 0.64, 0.63) + vec3<f32>(0.34, 0.27, 0.32)
    * cos(vec3<f32>(0.0, 2.1, 4.2) + (uv.x * 0.65 + uv.y * 0.35) * 6.28318);
  return palette * shape * band;
}
`;
  root.FawnFormation = Object.freeze({ POLICY, configure, strength, field, points, seed, WGSL });
})(globalThis);
