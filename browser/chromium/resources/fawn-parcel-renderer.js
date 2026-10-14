// Surface and point appearances reconstruct the same independently moving parcels.
const PARCEL_RENDER_POLICY = Object.freeze({
  version: 8,
  accumulationCapacity: 4096,
  materialBound: 8,
  fluidRadiusCells: 2.2,
  particleRadiusPx: 1.6,
  coverageGain: 2.5,
  exposure: 1.65,
  flowGlintPixels: 1.25,
  flowGlintStrength: 0.08,
  flowGlintSharpness: 12,
  flowSaturation: 0.7,
  backgroundGlintStrength: 0.032,
  filmPigmentMix: 0.8,
  filmAmbientLight: 0.18,
  liveFlowContrast: 0.18,
  liveFlowWall: Object.freeze([1.5, 5]),
});
const HEADER = `
struct Params { view:vec4<f32>, screen:vec4<f32>, theme:vec4<f32>, material:vec4<f32> }
@group(0) @binding(0) var<uniform> u:Params;
`;
const PARCEL_SPLATS =
  HEADER +
  `
struct Parcel { pos:vec2<f32>, vel:vec2<f32>, color:vec4<f32>, material:vec4<f32> }
@group(0) @binding(1) var<storage,read> parcels:array<Parcel>;
struct Out { @builtin(position) position:vec4<f32>, @location(0) local:vec2<f32>,
 @location(1) color:vec3<f32>, @location(2) material:vec4<f32> }
@vertex fn vs(@builtin(vertex_index) vertex:u32,@builtin(instance_index) instance:u32)->Out {
 let quad=array<vec2<f32>,4>(vec2<f32>(-1,-1),vec2<f32>(1,-1),vec2<f32>(-1,1),vec2<f32>(1,1));
 let p=parcels[instance];let local=quad[vertex];var out:Out;
 // Camera coordinates depend only on the accepted position and viewport.
 out.position=vec4<f32>((p.pos+local*u.view.z)/u.view.xy,0,1);
 out.local=local;out.color=clamp(p.color.rgb,vec3<f32>(0),vec3<f32>(${PARCEL_RENDER_POLICY.materialBound}));
 out.material=clamp(p.material,vec4<f32>(-${PARCEL_RENDER_POLICY.materialBound}),vec4<f32>(${PARCEL_RENDER_POLICY.materialBound}));return out;
}
struct Moments { @location(0) color:vec4<f32>, @location(1) geometry:vec4<f32> }
@fragment fn fs(in:Out)->Moments {
 let r2=dot(in.local,in.local);
 let fluidScale=u.view.z/max(u.material.z,0.000001);
 let pointScale=u.view.z/max(u.material.y,0.000001);
 let fluidKernel=pow(max(0.0,1.0-r2*fluidScale*fluidScale),2.0);
 let pointKernel=pow(max(0.0,1.0-r2*pointScale*pointScale),2.0);
 // Blend both endpoint footprints at the same live position, preserving transition coverage.
 let kernel=mix(fluidKernel,pointKernel,u.theme.z)*u.view.w;
 var out:Moments;out.color=vec4<f32>(in.color*kernel,kernel);out.geometry=in.material*kernel;return out;
}
`;
const PARCEL_SURFACE =
  HEADER +
  `
@group(0) @binding(1) var imageSampler:sampler;
@group(0) @binding(2) var colorField:texture_2d<f32>;
@group(0) @binding(3) var geometryField:texture_2d<f32>;
@group(0) @binding(4) var<storage,read> magneticField:array<vec4<f32>>;
@group(0) @binding(5) var<storage,read> flowDye:array<vec4<f32>>;
@group(0) @binding(6) var<storage,read> flowVelocity:array<vec4<f32>>;
@group(0) @binding(7) var<storage,read> flowCurl:array<vec4<f32>>;
fn flowCell(p:vec2<i32>)->u32 {
 let n=i32(u.material.w);let q=clamp(p,vec2<i32>(0),vec2<i32>(n-1));return u32(q.y*n+q.x);
}
fn flowRelief(d:vec3<f32>,v:vec2<f32>,c:f32)->f32 {
 return clamp(length(d)*0.64+abs(c)*0.016+length(v)*0.10,0.0,1.3);
}
// Live prismatic shading straight from the dye field keeps Flow's fine detail and color
// separation; parcels take over only as the material gathers into the mark. Alpha fades the
// no-slip wall cells, whose stagnant dye would otherwise draw a line at the sheet edge.
fn liveFlow(uv:vec2<f32>)->vec4<f32> {
 let n=u.material.w;let at=clamp(uv*n-0.5,vec2<f32>(0),vec2<f32>(n-1));
 let b=vec2<i32>(floor(at));let f=at-floor(at);
 let i00=flowCell(b);let i10=flowCell(b+vec2<i32>(1,0));
 let i01=flowCell(b+vec2<i32>(0,1));let i11=flowCell(b+vec2<i32>(1,1));
 let d00=flowDye[i00].rgb;let d10=flowDye[i10].rgb;let d01=flowDye[i01].rgb;let d11=flowDye[i11].rgb;
 let v00=flowVelocity[i00].xy;let v10=flowVelocity[i10].xy;let v01=flowVelocity[i01].xy;let v11=flowVelocity[i11].xy;
 let c00=flowCurl[i00].y;let c10=flowCurl[i10].y;let c01=flowCurl[i01].y;let c11=flowCurl[i11].y;
 let color=mix(mix(d00,d10,f.x),mix(d01,d11,f.x),f.y);
 let speed=length(mix(mix(v00,v10,f.x),mix(v01,v11,f.x),f.y));
 let curl=mix(mix(c00,c10,f.x),mix(c01,c11,f.x),f.y);
 let h00=flowRelief(d00,v00,c00);let h10=flowRelief(d10,v10,c10);
 let h01=flowRelief(d01,v01,c01);let h11=flowRelief(d11,v11,c11);
 let height=mix(mix(h00,h10,f.x),mix(h01,h11,f.x),f.y);
 let slope=vec2<f32>(mix(h10-h00,h11-h01,f.y),mix(h01-h00,h11-h10,f.x));
 let crisp=max(vec3<f32>(0),color+(color-(d00+d10+d01+d11)*0.25)*${PARCEL_RENDER_POLICY.liveFlowContrast});
 let normal=normalize(vec3<f32>(-slope*1.25,1));
 let fresnel=pow(1.0-max(normal.z,0.0),3.0);
 let prism=spectral(height*0.22+abs(curl)*0.36+u.screen.w*0.020);
 let secondary=spectral(speed*0.30+normal.x*0.15-normal.y*0.08);
 let glint=pow(max(reflect(normalize(vec3<f32>(0.20,-0.16,-1.0)),normal).z,0.0),20.0);
 let glow=crisp*(0.92+speed*0.24)+prism*abs(curl)*0.030;
 let metallic=mix(vec3<f32>(0.05,0.06,0.08),prism,0.52)+secondary*fresnel*0.22+vec3<f32>(1.0,0.96,0.93)*glint*0.34;
 let inset=min(min(at.x,at.y),min(n-1.0-at.x,n-1.0-at.y));
 return vec4<f32>(glow+metallic*(0.22+height*0.14),
   smoothstep(${PARCEL_RENDER_POLICY.liveFlowWall[0]},${PARCEL_RENDER_POLICY.liveFlowWall[1]},inset));
}
fn filmAt(uv:vec2<f32>)->vec4<f32> {
 let size=u.material.w;let p=clamp(uv*size-0.5,vec2<f32>(0),vec2<f32>(size-1));
 let b=vec2<u32>(floor(p));let f=fract(p);let q=min(b+vec2<u32>(1),vec2<u32>(u32(size)-1u));
 return mix(mix(magneticField[b.y*u32(size)+b.x],magneticField[b.y*u32(size)+q.x],f.x),
   mix(magneticField[q.y*u32(size)+b.x],magneticField[q.y*u32(size)+q.x],f.x),f.y);
}
struct Out { @builtin(position) position:vec4<f32>,@location(0) uv:vec2<f32> }
@vertex fn vs(@builtin(vertex_index) i:u32)->Out {
 let quad=array<vec2<f32>,4>(vec2<f32>(-1,-1),vec2<f32>(1,-1),vec2<f32>(-1,1),vec2<f32>(1,1));
 var out:Out;out.position=vec4<f32>(quad[i],0,1);out.uv=quad[i]*vec2<f32>(0.5,-0.5)+0.5;return out;
}
fn materialAt(uv:vec2<f32>)->vec4<f32> {
 let weight=textureSample(colorField,imageSampler,uv).a;
 return textureSample(geometryField,imageSampler,uv)/max(weight,0.000001);
}
fn spectral(t:f32)->vec3<f32> {return 0.5+0.5*cos(vec3<f32>(0,2,4)+t*6.28318);}
@fragment fn fs(in:Out)->@location(0) vec4<f32> {
 let color=textureSample(colorField,imageSampler,in.uv);let density=color.a/max(u.view.w,0.000001);
 let pigment=color.rgb/max(color.a,0.000001);let material=materialAt(in.uv);
 let l=materialAt(in.uv-vec2<f32>(u.screen.x,0)).x;
 let r=materialAt(in.uv+vec2<f32>(u.screen.x,0)).x;
 let b=materialAt(in.uv+vec2<f32>(0,u.screen.y)).x;
 let t=materialAt(in.uv-vec2<f32>(0,u.screen.y)).x;
 let gradient=vec2<f32>(r-l,t-b)*(u.screen.z*0.25);
 let normal=normalize(vec3<f32>(-gradient*0.12,1));
 let fresnel=pow(1.0-max(normal.z,0.0),3.0);
 let ridge=smoothstep(0.20,4.0,length(gradient));
 let phase=material.x*0.34+material.y*0.14+fresnel*0.55+dot(normal.xy,vec2<f32>(0.4,-0.3))+u.screen.w*0.015;
 let prism=spectral(phase);let contour=pow(0.5+0.5*sin(material.y*8.0-material.x*2.0),12.0);
 let light=normalize(vec3<f32>(0.45,0.60,1));let halfVector=normalize(light+vec3<f32>(0,0,1));
 let specular=pow(max(dot(normal,halfVector),0.0),64.0);
 let flowPrism=spectral(material.x*0.22+abs(material.z)*0.36+u.screen.w*0.020);
 // Soft highlights follow the carried relief; ambient light can move while the fluid rests.
 let facet=floor(in.position.xy/${PARCEL_RENDER_POLICY.flowGlintPixels});
 let grain=fract(sin(dot(facet,vec2<f32>(12.9898,78.233)))*43758.5453);
 let shimmer=pow(0.5+0.5*sin(grain*6.28318+material.x*8.0+material.z*3.0+u.material.x*0.65),${PARCEL_RENDER_POLICY.flowGlintSharpness}.0);
 let glint=pow(max(dot(reflect(normalize(vec3<f32>(0.20,-0.16,-1.0)),normal),vec3<f32>(0,0,1)),0.0),20.0);
 let secondary=spectral(material.w*0.30+normal.x*0.15-normal.y*0.08);
 let metallic=mix(vec3<f32>(0.05,0.06,0.08),flowPrism,0.52)
   +secondary*fresnel*0.22+vec3<f32>(1.0,0.96,0.93)*glint*0.34;
 let carriedFlowLight=pigment*(0.92+material.w*0.24)+flowPrism*abs(material.z)*0.030
   +metallic*(0.22+material.x*0.14)
   +vec3<f32>(1.0,0.96,0.93)*shimmer*(0.25+glint*0.75)*${PARCEL_RENDER_POLICY.flowGlintStrength};
 // Resolve the live magnetic relief directly: parcel moment averaging erases narrow bubble rings.
 let filmUV=(in.uv*vec2<f32>(2,-2)+vec2<f32>(-1,1))*u.view.xy*0.5+0.5;
 let live=liveFlow(filmUV);let liveWeight=(1.0-u.theme.w)*live.a;
 let flowLight=mix(carriedFlowLight,live.rgb,liveWeight);
 let c=filmAt(filmUV);let step=1.0/u.material.w;
 let filmGradient=vec2<f32>(filmAt(filmUV+vec2<f32>(step,0)).x-filmAt(filmUV-vec2<f32>(step,0)).x,
   filmAt(filmUV+vec2<f32>(0,step)).x-filmAt(filmUV-vec2<f32>(0,step)).x)*u.material.w*0.5;
 let filmNormal=normalize(vec3<f32>(-filmGradient*0.12,1));
 let filmFresnel=pow(1.0-max(filmNormal.z,0.0),3.0);
 let filmRidge=smoothstep(0.20,4.0,length(filmGradient));
 let fieldPhase=log(1.0+max(c.w,0.0));
 let filmPrism=spectral(c.x*0.34+fieldPhase*0.14+filmFresnel*0.55+dot(filmNormal.xy,vec2<f32>(0.4,-0.3))+u.screen.w*0.015);
 let filmContour=pow(0.5+0.5*sin(fieldPhase*8.0-c.x*2.0),12.0);
 let filmSpec=pow(max(dot(filmNormal,halfVector),0.0),64.0)*filmRidge;
 let waveLight=mix(pigment,filmPrism,${PARCEL_RENDER_POLICY.filmPigmentMix})*${PARCEL_RENDER_POLICY.filmAmbientLight}
   +filmPrism*filmRidge*(0.02+filmContour*0.40+filmFresnel*0.16)
   +vec3<f32>(0.8,0.88,1)*filmSpec*0.28;
 let fluidWeight=u.theme.x+u.theme.y;
 // Interpolate carried material in linear light before exposure, keeping relief-free film visible.
 let fluidLight=(flowLight*u.theme.x+waveLight*u.theme.y)/max(fluidWeight,0.000001);
 let fluidTone=1.0-exp(-max(fluidLight,vec3<f32>(0))*${PARCEL_RENDER_POLICY.exposure});
 let saturation=1.0-(1.0-${PARCEL_RENDER_POLICY.flowSaturation})*u.theme.x*(1.0-liveWeight);
 let fluidColor=mix(vec3<f32>(dot(fluidTone,vec3<f32>(0.2126,0.7152,0.0722))),fluidTone,saturation);
 let pointColor=pigment*(0.85+material.w*0.15);
 let carried=1.0-exp(-density*${PARCEL_RENDER_POLICY.coverageGain});
 // Live fluids fill the sheet until they gather into the mark; points always draw where they are.
 let coverage=carried+(1.0-carried)*(1.0-u.theme.w)*fluidWeight;
 let background=vec3<f32>(0.040,0.055,0.070)*u.theme.x+vec3<f32>(0.003,0.004,0.006)*u.theme.y
   +vec3<f32>(0.010,0.016,0.013)*u.theme.z
   +vec3<f32>(1.0,0.96,0.93)*shimmer*${PARCEL_RENDER_POLICY.backgroundGlintStrength}*u.theme.x;
 let appearance=fluidColor*fluidWeight+pointColor*u.theme.z;
 return vec4<f32>(mix(background,max(appearance,background),coverage),1);
}
`;
function createParcelRenderer(device, canvas, format, count, parcels, waves, grid, flow) {
  const uniform = device.createBuffer({
    label: 'Cycle parcel rendering uniforms',
    size: 64,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  const module = device.createShaderModule({ label: 'Fawn parcel splats', code: PARCEL_SPLATS });
  const blend = {
    color: { srcFactor: 'one', dstFactor: 'one', operation: 'add' },
    alpha: { srcFactor: 'one', dstFactor: 'one', operation: 'add' },
  };
  const splats = device.createRenderPipeline({
    label: 'Fawn moving material',
    layout: 'auto',
    vertex: { module, entryPoint: 'vs' },
    fragment: {
      module,
      entryPoint: 'fs',
      targets: [
        { format: 'rgba16float', blend },
        { format: 'rgba16float', blend },
      ],
    },
    primitive: { topology: 'triangle-strip' },
  });
  const lighting = device.createShaderModule({
    label: 'Fawn parcel surface lighting',
    code: PARCEL_SURFACE,
  });
  const surface = device.createRenderPipeline({
    label: 'Fawn full canvas surface',
    layout: 'auto',
    vertex: { module: lighting, entryPoint: 'vs' },
    fragment: { module: lighting, entryPoint: 'fs', targets: [{ format }] },
    primitive: { topology: 'triangle-strip' },
  });
  const groups = parcels.map((buffer) =>
    device.createBindGroup({
      layout: splats.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: { buffer: uniform } },
        { binding: 1, resource: { buffer } },
      ],
    }),
  );
  const sampler = device.createSampler({ minFilter: 'linear', magFilter: 'linear' });
  let textures = [],
    resolve;
  function resize() {
    textures.forEach((t) => t.destroy());
    textures = ['color', 'geometry'].map((kind) =>
      device.createTexture({
        label: `Cycle parcel ${kind} moments`,
        size: [canvas.width, canvas.height],
        format: 'rgba16float',
        usage:
          GPUTextureUsage.RENDER_ATTACHMENT |
          GPUTextureUsage.TEXTURE_BINDING |
          GPUTextureUsage.COPY_SRC,
      }),
    );
    // Indexed [wave field][dye field] to follow both solvers' ping-pong buffers.
    resolve = waves.map(wave => flow.dyes.map(dye => device.createBindGroup({
      layout: surface.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: { buffer: uniform } },
        { binding: 1, resource: sampler },
        { binding: 2, resource: textures[0].createView() },
        { binding: 3, resource: textures[1].createView() },
        { binding: 4, resource: { buffer: wave } },
        { binding: 5, resource: { buffer: dye } },
        { binding: 6, resource: { buffer: flow.velocity } },
        { binding: 7, resource: { buffer: flow.curl } },
      ],
    })));
  }
  function draw(encoder, view, index, weights, time, shimmerTime = time, waveIndex = 0, dyeIndex = 0,
    shape = 0) {
    const side = Math.max(canvas.clientWidth, canvas.clientHeight, 1);
    const fluidRadius = (2 / Math.sqrt(count)) * PARCEL_RENDER_POLICY.fluidRadiusCells;
    const particleRadius = (PARCEL_RENDER_POLICY.particleRadiusPx * 2) / side;
    const radius = weights[2] === 1 ? particleRadius : Math.max(fluidRadius, particleRadius);
    const normalization = Math.min(1, PARCEL_RENDER_POLICY.accumulationCapacity / count);
    device.queue.writeBuffer(
      uniform,
      0,
      new Float32Array([
        ...FawnFormation.viewport(canvas),
        radius,
        normalization,
        1 / canvas.width,
        1 / canvas.height,
        Math.max(canvas.width, canvas.height),
        time,
        ...weights,
        shape,
        shimmerTime,
        particleRadius,
        fluidRadius,
        grid,
      ]),
    );
    const pass = encoder.beginRenderPass({
      label: 'Fawn per-parcel material',
      colorAttachments: textures.map((t) => ({
        view: t.createView(),
        loadOp: 'clear',
        storeOp: 'store',
        clearValue: [0, 0, 0, 0],
      })),
    });
    pass.setPipeline(splats);
    pass.setBindGroup(0, groups[index]);
    pass.draw(4, count);
    pass.end();
    const output = encoder.beginRenderPass({
      label: 'Fawn full canvas lighting',
      colorAttachments: [{ view, loadOp: 'clear', storeOp: 'store', clearValue: [0, 0, 0, 1] }],
    });
    output.setPipeline(surface);
    output.setBindGroup(0, resolve[waveIndex][dyeIndex]);
    output.draw(4);
    output.end();
  }
  resize();
  return {
    resize,
    draw,
    destroy() {
      uniform.destroy();
      textures.forEach((t) => t.destroy());
    },
  };
}
export { PARCEL_RENDER_POLICY, PARCEL_SPLATS, PARCEL_SURFACE, createParcelRenderer };
