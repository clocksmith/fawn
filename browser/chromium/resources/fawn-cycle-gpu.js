// Resident solvers share a device, coordinates, and one source-preserving handoff path.
import { flow, wave, particles, updateWaveSources } from './fawn-shaders.js';
import { FLOW_TO_WAVE, WAVE_TO_PARTICLES, PARTICLE_DEPOSIT, RESOLVE_DEPOSIT } from './fawn-cycle-transfers.js';
const GPU_POLICY = Object.freeze({ version: 1, maxCount: 262144, pressureIterations: 12, radiusPx: 1.6, surfaceGroup: 16, particleGroup: 128 });
function createCycleGpu(device, canvas, format, count) {
  const grid = Math.sqrt(count), buffers = [], textures = [];
  const buffer = (label, size, uniform = false) => {
    const b = device.createBuffer({ label, size, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC |
      (uniform ? GPUBufferUsage.UNIFORM : GPUBufferUsage.STORAGE) });
    buffers.push(b); return b;
  };
  const field = label => buffer(label, count * 16);
  const compute = (label, code) => device.createComputePipeline({ label, layout: 'auto',
    compute: { module: device.createShaderModule({ label, code }), entryPoint: 'main' } });
  const render = (label, code, blend) => {
    const module = device.createShaderModule({ label, code });
    return device.createRenderPipeline({ label, layout: 'auto', vertex: { module, entryPoint: 'vs' },
      fragment: { module, entryPoint: 'fs', targets: [{ format, ...(blend ? { blend } : {}) }] },
      primitive: { topology: 'triangle-strip' } });
  };
  const group = (pipeline, bindings) => device.createBindGroup({ layout: pipeline.getBindGroupLayout(0),
    entries: bindings.map(([binding, resource]) => ({ binding, resource: resource instanceof GPUBuffer ? { buffer: resource } : resource })) });
  const sequential = (pipeline, resources) => group(pipeline, resources.map((r, i) => [i, r]));
  const dispatch = (encoder, pipeline, bindings, surface = false) => {
    const pass = encoder.beginComputePass(); pass.setPipeline(pipeline); pass.setBindGroup(0, bindings);
    if (surface) pass.dispatchWorkgroups(Math.ceil(grid / GPU_POLICY.surfaceGroup), Math.ceil(grid / GPU_POLICY.surfaceGroup));
    else pass.dispatchWorkgroups(Math.ceil(count / GPU_POLICY.particleGroup));
    pass.end();
  };
  const flowUniform = buffer('Cycle Flow uniforms', 80, true);
  const waveUniform = buffer('Cycle Wave uniforms', wave.WAVE_UNIFORM_FLOATS * 4, true);
  const particleUniform = buffer('Cycle Particles uniforms', 64, true);
  const transferUniform = buffer('Cycle transfer uniforms', 48, true);
  const logo = field('Cycle material destinations');
  const logoPoints = field('Cycle particle targets');
  const velocities = [field('Cycle velocity 0'), field('Cycle velocity 1')];
  const dyes = [field('Cycle dye 0'), field('Cycle dye 1')];
  const pressures = [buffer('Cycle pressure 0', count * 4), buffer('Cycle pressure 1', count * 4)];
  const curl = field('Cycle divergence and curl');
  const waves = [field('Cycle surface 0'), field('Cycle surface 1')];
  const points = [buffer('Cycle particles 0', count * 32), buffer('Cycle particles 1', count * 32)];
  const deposits = buffer('Cycle deposits', count * 24);
  let dyeIndex = 0, waveIndex = 0, particleIndex = 0;
  let destinations, targets = [], trailTargets = [], trailIndex = 0;
  const flowCompute = ['ADVECT_VEL_WGSL', 'DIV_CURL_WGSL', 'PRESSURE_WGSL', 'PROJECT_WGSL', 'ADVECT_DYE_WGSL']
    .map(name => compute(`Cycle Flow ${name}`, flow[name]));
  const flowRender = render('Cycle Flow material', flow.RENDER_WGSL);
  // Reflection stays driven by the live surface, with pigment inherited from the preceding fluid.
  const waveCode = wave.RENDER_WGSL.replace('@fragment', '@group(0) @binding(3) var<storage,read> pigment: array<vec4<f32>>;\n@fragment')
    .replace('let prism = spectral(', 'let prismBase = spectral(')
    .replace('let contour =', `let pigmentColor = pigment[idx_at(i32(in.uv.x*u.grid.x),i32(in.uv.y*u.grid.y))].rgb;
      let prism = mix(prismBase, pigmentColor / max(max(pigmentColor.r,pigmentColor.g),max(pigmentColor.b,0.001)),0.32);
      let contour =`);
  const waveCompute = compute('Cycle Wave solver', wave.COMPUTE_WGSL);
  const waveRender = render('Cycle Wave material', waveCode);
  const particleCompute = compute('Cycle Particles solver', particles.COMPUTE_WGSL);
  // All effects deform the same material sheet, so handoffs never change logo coordinates.
  const particleCode = particles.PARTICLE_WGSL
    .replace('@vertex', '@group(0) @binding(2) var<storage,read> destinations: array<vec4<f32>>;\n@vertex')
    .replace('let world = (p.pos + local * vec2<f32>(size)) / u.viewport.xy;', `let uv=p.pos*0.5+0.5;
      let destination=mix(destinations[0].xy,destinations[1].xy,uv);
      let mapped=mix(uv,destination,u.formation.x);
      let world=(mapped*2.0-1.0+local*vec2<f32>(size))/u.viewport.xy;`)
    .replace('out.color = mix(p.color.xyz, palette, u.formation.x)', 'out.color = p.color.xyz')
    .replace('out.visibility = 1.0 - smoothstep(', `let cx=clamp(i32(mapped.x*u.viewport.z),0,i32(u.viewport.z)-1);
      let cy=clamp(i32(mapped.y*u.viewport.z),0,i32(u.viewport.z)-1);
      let mark=destinations[u32(cy)*u32(u.viewport.z)+u32(cx)].z;
      out.visibility = mix(1.0,mark,smoothstep(0.35,1.0,u.formation.x)) * (1.0 - smoothstep(`)
    .replace('max(abs(p.pos.x), abs(p.pos.y)));', 'max(abs(p.pos.x), abs(p.pos.y))));');
  const particleRender = render('Cycle Particles material', particleCode, particles.PARTICLE_BLEND);
  const fadeCode = particles.FADE_WGSL.replace('array<vec2<f32>, 3>', 'array<vec2<f32>, 4>')
    .replace('vec2<f32>(-1.0, -3.0),', 'vec2<f32>(-1.0, -1.0),')
    .replace('vec2<f32>( 3.0,  1.0)', 'vec2<f32>(1.0, -1.0), vec2<f32>(1.0,1.0)');
  // Strip order is bottom-left, top-left, bottom-right, top-right.
  const fade = render('Cycle particle trails', fadeCode);
  const transfers = [FLOW_TO_WAVE, WAVE_TO_PARTICLES, PARTICLE_DEPOSIT, RESOLVE_DEPOSIT]
    .map((code, i) => compute(`Cycle handoff ${i}`, code));
  const flowGroups = {
    advect: sequential(flowCompute[0], [flowUniform, velocities[0], velocities[1]]),
    curl: sequential(flowCompute[1], [flowUniform, velocities[1], curl]),
    pressure: [0, 1].map(i => sequential(flowCompute[2], [flowUniform, pressures[i], curl, pressures[1-i]])),
    project: sequential(flowCompute[3], [flowUniform, velocities[1], pressures[0], curl, velocities[0]]),
    dye: [0, 1].map(i => sequential(flowCompute[4], [flowUniform, dyes[i], velocities[0], dyes[1-i]])),
    render: [0, 1].map(i => sequential(flowRender, [flowUniform, dyes[i], velocities[0], curl, logo])),
  };
  const waveGroups = [0, 1].map(i => sequential(waveCompute, [waveUniform, waves[i], waves[1-i]]));
  const waveRenders = [0,1].map(i => [0,1].map(j => sequential(waveRender,[waveUniform,waves[i],logo,dyes[j]])));
  const particleGroups = [0, 1].map(i => sequential(particleCompute, [particleUniform, points[i], points[1-i], logoPoints]));
  const particleRenders = [0, 1].map(i => sequential(particleRender, [particleUniform, points[i], logo]));
  const sampler = device.createSampler({ minFilter: 'linear', magFilter: 'linear' });
  let fadeGroups = [];
  function updateLogo() {
    destinations = FawnFormation.materialDestinations(grid, canvas, document.querySelector('.brand-logo'));
    device.queue.writeBuffer(logo, 0, destinations);
  }
  function resize() {
    textures.splice(0).forEach(t => t.destroy());
    const target = label => {
      const t = device.createTexture({ label, size: [canvas.width, canvas.height], format,
        usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC });
      textures.push(t); return t;
    };
    targets = [target('Cycle effect image 0'), target('Cycle effect image 1')];
    trailTargets = [target('Cycle particle history 0'), target('Cycle particle history 1')];
    fadeGroups = trailTargets.map(t => sequential(fade, [sampler, t.createView()]));
    trailIndex = 0; updateLogo();
  }
  function reset() {
    const seed = FawnFormation.seed(grid);
    dyes.forEach(b => device.queue.writeBuffer(b, 0, seed.dye));
    velocities.forEach(b => device.queue.writeBuffer(b, 0, seed.velocity));
    const encoder = device.createCommandEncoder();
    [...waves, ...points, ...pressures, curl, deposits].forEach(b => encoder.clearBuffer(b));
    device.queue.submit([encoder.finish()]);
    dyeIndex = 0; waveIndex = 0; particleIndex = 0;
    resize();
  }
  function uniforms(dt, time, shape, pointer, steps) {
    const viewport = FawnFormation.viewport(canvas);
    const mapped = FawnFormation.materialPoint([pointer.x, pointer.y], destinations, shape);
    const previous = FawnFormation.materialPoint([pointer.x-pointer.dx, pointer.y-pointer.dy], destinations, shape);
    const force = pointer.hover ? (pointer.down ? 1.4 : 0.28) : 0;
    device.queue.writeBuffer(flowUniform, 0, new Float32Array([grid,grid,1/grid,1/grid,...mapped,force,force>0.28?0.1:0.05,
      dt*0.82/steps,time,2,shape,mapped[0]-previous[0],mapped[1]-previous[1],pointer.press,0,...viewport,steps,0]));
    const w = new Float32Array(wave.WAVE_UNIFORM_FLOATS);
    w.set([grid,grid,1/grid,1/grid,...mapped,pointer.hover?(pointer.down?1:0.55):0,0,dt*1.1/steps,time,canvas.width/canvas.height,shape]);
    updateWaveSources(w, time); device.queue.writeBuffer(waveUniform, 0, w);
    const radius = GPU_POLICY.radiusPx * (4096/count)**0.25 * 2/Math.max(canvas.clientWidth,canvas.clientHeight);
    device.queue.writeBuffer(particleUniform, 0, new Float32Array([dt/steps,time,canvas.width/canvas.height,pointer.down?26.67:0,
      pointer.x*2-1,pointer.y*2-1,radius,0,0,0,0,0,...viewport,grid,0]));
    // Particle simulation remains in source coordinates; only its render deforms the sheet.
    const pRender = new Float32Array([dt/steps,time,canvas.width/canvas.height,0,0,0,radius,0,shape,0,0,0,...viewport,grid,0]);
    return pRender;
  }
  function simulate(encoder, effect, steps) {
    if (effect === 'flow') {
      for(let step=0;step<steps;step++) {
        dispatch(encoder,flowCompute[0],flowGroups.advect,true); dispatch(encoder,flowCompute[1],flowGroups.curl,true);
        for(let i=0;i<GPU_POLICY.pressureIterations;i++) dispatch(encoder,flowCompute[2],flowGroups.pressure[i%2],true);
        dispatch(encoder,flowCompute[3],flowGroups.project,true);
        dispatch(encoder,flowCompute[4],flowGroups.dye[dyeIndex],true); dyeIndex=1-dyeIndex;
      }
    } else if(effect==='wave') {
      for(let i=0;i<steps;i++){dispatch(encoder,waveCompute,waveGroups[waveIndex],true);waveIndex=1-waveIndex;}
    } else {
      for(let i=0;i<steps;i++){dispatch(encoder,particleCompute,particleGroups[particleIndex]);particleIndex=1-particleIndex;}
    }
  }
  function handoff(encoder, effect, shape, sourceImage) {
    const viewport = FawnFormation.viewport(canvas);
    device.queue.writeBuffer(transferUniform,0,new Float32Array([grid,grid,1/grid,1/grid,...viewport,shape,0,
      destinations[0],destinations[1],destinations[4],destinations[5]]));
    if(effect==='flow') {
      dispatch(encoder,transfers[0],sequential(transfers[0],[transferUniform,dyes[dyeIndex],velocities[0],waves[0]])); waveIndex=0;
    } else if(effect==='wave') {
      dispatch(encoder,transfers[1],sequential(transfers[1],[transferUniform,waves[waveIndex],dyes[dyeIndex],points[0],sourceImage.createView()])); particleIndex=0;
      // Trails inherit the visible film on their first frame, then decay normally.
      const pass=encoder.beginRenderPass({colorAttachments:[{view:trailTargets[0].createView(),loadOp:'clear',storeOp:'store',clearValue:[0,0,0,1]}]});
      pass.setPipeline(fade);pass.setBindGroup(0,sequential(fade,[sampler,sourceImage.createView()]));pass.draw(4);pass.end();trailIndex=0;
    } else {
      encoder.clearBuffer(deposits);
      dispatch(encoder,transfers[2],sequential(transfers[2],[transferUniform,points[particleIndex],deposits]));
      dispatch(encoder,transfers[3],sequential(transfers[3],[transferUniform,deposits,dyes[0],velocities[0]]));
      encoder.clearBuffer(pressures[0]);encoder.clearBuffer(pressures[1]);encoder.clearBuffer(curl);dyeIndex=0;
    }
  }
  function draw(encoder, effect, slot, particleRenderUniform) {
    const texture=targets[slot];
    const background=effect==='flow'?flow.FLOW_MATERIAL.background:effect==='wave'?[0.003,0.004,0.006]:[0.010,0.016,0.013];
    const attachment=view=>({view,loadOp:'clear',storeOp:'store',clearValue:[...background,1]});
    if(effect==='particles') {
      // Separate render uniforms avoid one queue write changing already-encoded compute uniforms.
      device.queue.writeBuffer(particleRenderUniformBuffer,0,particleRenderUniform);
      const next=1-trailIndex;
      const pass=encoder.beginRenderPass({colorAttachments:[attachment(trailTargets[next].createView())]});
      pass.setPipeline(fade);pass.setBindGroup(0,fadeGroups[trailIndex]);pass.draw(4);
      pass.setPipeline(particleRender);pass.setBindGroup(0,particleRenderGroups[particleIndex]);pass.draw(4,count);pass.end();
      const image=encoder.beginRenderPass({colorAttachments:[attachment(texture.createView())]});
      image.setPipeline(copy);image.setBindGroup(0,sequential(copy,[sampler,trailTargets[next].createView()]));image.draw(4);image.end();trailIndex=next;
    } else {
      const pass=encoder.beginRenderPass({colorAttachments:[attachment(texture.createView())]});
      pass.setPipeline(effect==='flow'?flowRender:waveRender);
      pass.setBindGroup(0,effect==='flow'?flowGroups.render[dyeIndex]:waveRenders[waveIndex][dyeIndex]);pass.draw(4);pass.end();
    }
    return texture;
  }
  const copy=render('Cycle image copy',fadeCode.replace('return vec4<f32>(max(faded, background), 1.0);','return vec4<f32>(prev.rgb,1.0);'));
  const particleRenderUniformBuffer=buffer('Cycle particle render uniforms',64,true);
  const particleRenderGroups=[0,1].map(i=>sequential(particleRender,[particleRenderUniformBuffer,points[i],logo]));
  reset();
  return { uniforms, simulate, handoff, draw, resize, reset, textures:()=>targets,
    destroy(){buffers.forEach(b=>b.destroy());textures.forEach(t=>t.destroy());} };
}
export { GPU_POLICY, createCycleGpu };
