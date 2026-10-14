(function attachFawnFormation(root) {
  'use strict';
  const POLICY = Object.freeze({ version: 4, delayS: 0.8, settleS: 3,
    interactionQuietS: 1, size: 0.76,
    noiseSeed: 7319, dyeRate: 1.6, diffusionRate: 5, force: 0.7, warp: 0.05 });
  const TRANSITION = Object.freeze({ version: 2, holdS: 0.3, disperseS: 1.6,
    responseRate: 8, epsilon: 0.00001, disperseForce: 0.9 });
  const PALETTE = Object.freeze({ base: Object.freeze([0.52, 0.64, 0.63]),
    span: Object.freeze([0.34, 0.27, 0.32]), phase: 2.1 });

  const WORKLOAD = Object.freeze({ version: 1, counts: [1024, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216],
    cycleCounts: [1024, 4096, 16384, 32768, 65536, 262144],
    steps: [1, 2, 4, 8], defaultCount: 65536, defaultSteps: 4, mobileCycleCount: 16384,
    defaultCounts: Object.freeze({ flow: 65536, wave: 65536, particles: 4096, cycle: 65536 }) });

  function configureWorkload(count, steps, kind, mobile = false) {
    if (!(kind in WORKLOAD.defaultCounts)) throw Error(`Unknown Fawn workload: ${kind}`);
    count.replaceChildren(...(kind === 'cycle' ? WORKLOAD.cycleCounts : WORKLOAD.counts).map(value => new Option(value.toLocaleString(), String(value))));
    steps.replaceChildren(...WORKLOAD.steps.map(value => new Option(String(value), String(value))));
    count.value = String(kind === 'cycle' && mobile ? WORKLOAD.mobileCycleCount : WORKLOAD.defaultCounts[kind]); steps.value = String(WORKLOAD.defaultSteps);
  }

  function limitCount(select, maximum) {
    for (const option of select.options) {
      option.disabled = Number(option.value) > maximum;
      if (option.disabled) option.textContent += ' (GPU limit)';
    }
    if (Number(select.value) > maximum) {
      const available = [...select.options].filter(option => !option.disabled);
      if (!available.length) throw Error('GPU storage limit cannot support this workload');
      select.value = available.at(-1).value;
    }
  }

  // Cover a square world: crop extra space instead of stretching its axes.
  function viewport(canvas) {
    const side = Math.max(canvas.clientWidth, canvas.clientHeight, 1);
    return [canvas.clientWidth / side, canvas.clientHeight / side];
  }

  function screenPoint(event, canvas) {
    const rect = canvas.getBoundingClientRect(), side = Math.max(rect.width, rect.height, 1);
    return [0.5 + (event.clientX - rect.left - rect.width / 2) / side,
      0.5 - (event.clientY - rect.top - rect.height / 2) / side];
  }

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

  function ease(t) {
    return t ** 3 * (t * (t * 6 - 15) + 10);
  }

  function strength(ageS, enabled, schedule = POLICY) {
    if (!enabled) return 0;
    const delay = schedule.delayS, settle = schedule.settleS;
    return ease(Math.min(1, Math.max(0, (ageS - delay) / settle)));
  }

  function transition(schedule = POLICY) {
    const responseRate = schedule.responseRate ?? TRANSITION.responseRate;
    let age = 0, idle = 0, opening = false, quiet = 0, value = 0, velocity = 0;
    let releaseStart = null;
    return Object.freeze({
      reset(enabled, intro = false) {
        age = idle = velocity = 0; quiet = 0; releaseStart = null;
        opening = enabled && intro; value = opening ? 1 : 0;
      },
      advance(dt, enabled, active, interacting = active, releaseProgress = 1) {
        if (dt <= 0) return value;
        const previousQuiet = quiet;
        quiet = interacting ? POLICY.interactionQuietS : Math.max(0, quiet - dt);
        const settlingDt = interacting ? 0 : Math.max(0, dt - previousQuiet);
        if (!active || interacting || !enabled) releaseStart = null;
        else if (releaseStart === null) releaseStart = value;
        if (!enabled) { idle = 0; quiet = 0; }
        if (!enabled || active) opening = false;
        age += dt;
        let target;
        if (opening) {
          const t = Math.min(1, Math.max(0, (age - TRANSITION.holdS) / TRANSITION.disperseS));
          idle = Math.max(0, age - TRANSITION.holdS - TRANSITION.disperseS);
          target = Math.max(1 - ease(t), strength(idle, true, schedule));
        } else {
          if (!active && enabled) idle += settlingDt;
          target = active || quiet > 0 ? 0 : strength(idle, enabled, schedule);
          // Scheduled release follows the appearance blend; input still releases immediately.
          if (releaseStart !== null) target = releaseStart * (1 - releaseProgress);
        }
        // Preserve value and velocity as targets change, including interruptions.
        const offset = value - target, rate = velocity + responseRate * offset;
        const decay = Math.exp(-responseRate * dt);
        value = target + (offset + rate * dt) * decay;
        velocity = (velocity - responseRate * rate * dt) * decay;
        if (Math.abs(value - target) < TRANSITION.epsilon && Math.abs(velocity) < TRANSITION.epsilon) {
          value = target; velocity = 0;
        }
        return Math.max(0, Math.min(1, value));
      },
      disperse() {
        if (!opening) return 0;
        const t = (age - TRANSITION.holdS) / TRANSITION.disperseS;
        return t > 0 && t < 1 ? Math.sin(t * Math.PI) ** 2 * TRANSITION.disperseForce : 0;
      },
      retarget() { opening = false; idle = 0; releaseStart = null; },
    });
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
    const side = Math.max(canvas.clientWidth, canvas.clientHeight, 1);
    const width = box.width * scale / side * grid;
    const height = box.height * scale / side * grid;
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

  function seed(grid, mark = null) {
    const dye = new Float32Array(grid * grid * 4), velocity = new Float32Array(dye.length);
    let random = POLICY.noiseSeed;
    for (let y = 0; y < grid; y++) for (let x = 0; x < grid; x++) {
      const i = (y * grid + x) * 4, u = x / grid, v = y / grid;
      random ^= random << 13; random ^= random >>> 17; random ^= random << 5;
      const grain = (random >>> 0) / 4294967296;
      const cloud = 0.35 + 0.3 * Math.sin(u * 21 + Math.sin(v * 13)) * Math.cos(v * 18 - u * 6);
      for (let c = 0; c < 3; c++) {
        if (mark) {
          const palette = PALETTE.base[c] + PALETTE.span[c]
            * Math.cos(c * PALETTE.phase + (u * 0.65 + v * 0.35) * Math.PI * 2);
          dye[i + c] = palette * mark[i + 2] * (0.82 + 0.18 * Math.sin(mark[i + 3] * 110 + v * 8));
        } else dye[i + c] = cloud * (0.55 + 0.45 * Math.cos(u * 9 + v * 7 + c * 2.1)) + grain * 0.055;
      }
      dye[i + 3] = 1;
      velocity[i] = 0.09 * Math.sin(v * Math.PI * 4) * Math.sin(u * Math.PI);
      velocity[i + 1] = -0.09 * Math.sin(u * Math.PI * 4) * Math.sin(v * Math.PI);
    }
    return { dye, velocity };
  }

  // Each particle target stores square-world XY, inward distance, and one reserved float.
  function points(grid, canvas, logo) {
    const raster = field(grid, canvas, logo), result = [];
    for (let i = 0; i < grid * grid; i++) if (raster[i * 4 + 2] > 0.5) {
      result.push(((i % grid) + 0.5) / grid * 2 - 1,
        (Math.floor(i / grid) + 0.5) / grid * 2 - 1, Math.max(0, raster[i * 4 + 3]) * 2, 0);
    }
    if (!result.length) throw Error('Fawn formation requires visible logo geometry');
    return new Float32Array(result);
  }


  // A continuous sheet carries the live field into the mark; no new dye or height is seeded.
  function materialDestinations(grid, canvas, logo) {
    const mark = field(grid, canvas, logo);
    let x0 = 1, y0 = 1, x1 = 0, y1 = 0;
    for (let i = 0; i < grid * grid; i++) if (mark[i * 4 + 2] > 0.5) {
      const x = i % grid, y = Math.floor(i / grid);
      x0 = Math.min(x0, x / grid); y0 = Math.min(y0, y / grid);
      x1 = Math.max(x1, (x + 1) / grid); y1 = Math.max(y1, (y + 1) / grid);
    }
    if (x1 <= x0 || y1 <= y0) throw Error('Fawn material requires visible target geometry');
    mark[0] = x0; mark[1] = y0; mark[4] = x1; mark[5] = y1;
    return mark;
  }

  function materialPoint(point, destinations, blend) {
    if (blend <= 0 || !destinations) return point;
    const left = destinations[0] * blend, bottom = destinations[1] * blend;
    const width = 1 + (destinations[4] - destinations[0] - 1) * blend;
    const height = 1 + (destinations[5] - destinations[1] - 1) * blend;
    return [(point[0] - left) / width, (point[1] - bottom) / height];
  }

  const MATERIAL_POLICY = Object.freeze({ version: 1, shapeStart: 0.35 });
  const MATERIAL_VERTEX = `
fn materialCoverage(uv: vec2<f32>) -> f32 {
  let p = clamp(uv * u.grid.xy - 0.5, vec2<f32>(0.0), u.grid.xy - 1.0);
  let base = vec2<i32>(floor(p)); let f = fract(p);
  let coverage = mix(mix(materialDestinations[idx_at(base.x, base.y)].z,
    materialDestinations[idx_at(base.x + 1, base.y)].z, f.x),
    mix(materialDestinations[idx_at(base.x, base.y + 1)].z,
      materialDestinations[idx_at(base.x + 1, base.y + 1)].z, f.x), f.y);
  // Geometry moves first. Only its final silhouette is constrained to the mark.
  let shape = smoothstep(${MATERIAL_POLICY.shapeStart}, 1.0, u.sim.w);
  return mix(1.0, coverage, shape);
}
@vertex
fn vs(@builtin(vertex_index) vertexIndex: u32) -> VSOut {
  let corners = array<vec2<f32>, 4>(vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0),
    vec2<f32>(0.0, 1.0), vec2<f32>(1.0, 1.0));
  let source = corners[vertexIndex];
  let destination = mix(materialDestinations[0].xy, materialDestinations[1].xy, source);
  let position = mix(source, destination, u.sim.w);
  var out: VSOut;
  out.position = vec4<f32>((position * 2.0 - 1.0) / materialViewport(), 0.0, 1.0);
  out.uv = source;
  out.materialUV = position;
  return out;
}
`;

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
  let palette = vec3<f32>(${PALETTE.base.join(', ')}) + vec3<f32>(${PALETTE.span.join(', ')})
    * cos(vec3<f32>(0.0, ${PALETTE.phase}, ${PALETTE.phase * 2}) + (uv.x * 0.65 + uv.y * 0.35) * 6.28318);
  return palette * shape * band;
}
`;
  root.FawnFormation = Object.freeze({ POLICY, TRANSITION, WORKLOAD, configureWorkload, limitCount, viewport, screenPoint, configure, strength, transition, field, points, seed, materialDestinations, materialPoint, MATERIAL_POLICY, MATERIAL_VERTEX, WGSL });
})(globalThis);
