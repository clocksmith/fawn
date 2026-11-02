// Legacy transported-sheet formation used only by retained fluid benchmarks.
(function(root) {
  const { field, POLICY, PALETTE } = root.FawnFormation;
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
  root.FawnFormation = Object.freeze({ ...root.FawnFormation, materialDestinations, materialPoint, MATERIAL_POLICY, MATERIAL_VERTEX, WGSL });
})(globalThis);
