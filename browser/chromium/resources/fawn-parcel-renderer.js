// Surface and point appearances reconstruct the same independently moving parcels.
const PARCEL_RENDER_POLICY = Object.freeze({
  version: 3,
  accumulationCapacity: 4096,
  materialBound: 8,
  fluidRadiusCells: 2.2,
  particleRadiusPx: 1.6,
  coverageGain: 2.5,
  exposure: 1.65,
  flowGlintPixels: 1.25,
  flowGlintStrength: 0.16,
  flowGlintSharpness: 32,
  backgroundGlintStrength: 0.012,
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
 let r2=dot(in.local,in.local);let kernel=pow(max(0.0,1.0-r2),2.0)*u.view.w;
 var out:Moments;out.color=vec4<f32>(in.color*kernel,kernel);out.geometry=in.material*kernel;return out;
}
`;
const PARCEL_SURFACE =
  HEADER +
  `
@group(0) @binding(1) var imageSampler:sampler;
@group(0) @binding(2) var colorField:texture_2d<f32>;
@group(0) @binding(3) var geometryField:texture_2d<f32>;
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
 // Fine facets catch the changing carried material without disturbing its broad relief.
 let facet=floor(in.position.xy/${PARCEL_RENDER_POLICY.flowGlintPixels});
 let grain=fract(sin(dot(facet,vec2<f32>(12.9898,78.233)))*43758.5453);
 let shimmer=pow(0.5+0.5*sin(grain*6.28318+material.x*32.0+material.z*8.0+u.screen.w*1.8),${PARCEL_RENDER_POLICY.flowGlintSharpness}.0);
 let flowLight=pigment*(0.92+material.w*0.24)+flowPrism*abs(material.z)*0.030
   +(prism*(0.22+material.x*0.14)+vec3<f32>(specular*0.30))
   +mix(prism,vec3<f32>(1.0),0.5)*shimmer*${PARCEL_RENDER_POLICY.flowGlintStrength};
 let flowColor=1.0-exp(-max(flowLight,vec3<f32>(0))*${PARCEL_RENDER_POLICY.exposure});
 let waveColor=prism*ridge*(0.02+contour*0.40+fresnel*0.16)+vec3<f32>(0.8,0.88,1)*specular*ridge*0.28;
 let pointColor=pigment*(0.85+material.w*0.15);
 let coverage=1.0-exp(-density*${PARCEL_RENDER_POLICY.coverageGain});
 let background=vec3<f32>(0.040,0.055,0.070)*u.theme.x+vec3<f32>(0.003,0.004,0.006)*u.theme.y
   +vec3<f32>(0.010,0.016,0.013)*u.theme.z
   +mix(prism,vec3<f32>(1.0),0.5)*shimmer*${PARCEL_RENDER_POLICY.backgroundGlintStrength}*u.theme.x;
 let appearance=flowColor*u.theme.x+waveColor*u.theme.y+pointColor*u.theme.z;
 return vec4<f32>(mix(background,max(appearance,background),coverage),1);
}
`;
function createParcelRenderer(device, canvas, format, count, parcels) {
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
    resolve = device.createBindGroup({
      layout: surface.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: { buffer: uniform } },
        { binding: 1, resource: sampler },
        { binding: 2, resource: textures[0].createView() },
        { binding: 3, resource: textures[1].createView() },
      ],
    });
  }
  function draw(encoder, view, index, weights, time) {
    const side = Math.max(canvas.clientWidth, canvas.clientHeight, 1);
    const fluidRadius = (2 / Math.sqrt(count)) * PARCEL_RENDER_POLICY.fluidRadiusCells;
    const particleRadius = (PARCEL_RENDER_POLICY.particleRadiusPx * 2) / side;
    const radius = fluidRadius + (particleRadius - fluidRadius) * weights[2];
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
        0,
        0,
        0,
        0,
        0,
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
    output.setBindGroup(0, resolve);
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
