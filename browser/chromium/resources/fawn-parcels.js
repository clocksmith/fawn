// Persistent moving material. Formation edits parcel positions, never the camera or a quad.
const PARCEL_POLICY = Object.freeze({
  version: 3,
  strideFloats: 12,
  workgroup: 128,
  targetGrid: 384,
  speedLimit: 1.75,
  fieldSpeed: 12,
  velocityResponse: 12,
  colorResponse: 3,
  settleRate: 2,
  settleDamping: 8,
  worldEdge: 1.65,
  initialRadius: 1.65,
});
const PARCEL_UPDATE = `
struct Parcel { pos: vec2<f32>, vel: vec2<f32>, color: vec4<f32>, material: vec4<f32> }
struct Params { grid: vec4<f32>, theme: vec4<f32>, pointer: vec4<f32>, burst: vec4<f32> }
@group(0) @binding(0) var<uniform> u: Params;
@group(0) @binding(1) var<storage,read> src: array<Parcel>;
@group(0) @binding(2) var<storage,read_write> dst: array<Parcel>;
@group(0) @binding(3) var<storage,read> goals: array<vec4<f32>>;
@group(0) @binding(4) var<storage,read> flowVelocity: array<vec4<f32>>;
@group(0) @binding(5) var<storage,read> flowDye: array<vec4<f32>>;
@group(0) @binding(6) var<storage,read> waveSurface: array<vec4<f32>>;
@group(0) @binding(7) var<storage,read> flowCurl: array<vec4<f32>>;
fn cell(p:vec2<i32>)->u32 {
 let q=clamp(p,vec2<i32>(0),vec2<i32>(i32(u.grid.x)-1));return u32(q.y)*u32(u.grid.x)+u32(q.x);
}
fn flow_at(pos:vec2<f32>)->vec4<f32> {
 let p=clamp((pos*0.5+0.5)*u.grid.x-0.5,vec2<f32>(0),vec2<f32>(u.grid.x-1));let b=vec2<i32>(floor(p));let f=fract(p);
 return mix(mix(flowVelocity[cell(b)],flowVelocity[cell(b+vec2<i32>(1,0))],f.x),
   mix(flowVelocity[cell(b+vec2<i32>(0,1))],flowVelocity[cell(b+vec2<i32>(1,1))],f.x),f.y);
}
fn wave_at(pos:vec2<f32>)->vec4<f32> {
 let p=clamp((pos*0.5+0.5)*u.grid.x-0.5,vec2<f32>(0),vec2<f32>(u.grid.x-1));let b=vec2<i32>(floor(p));let f=fract(p);
 return mix(mix(waveSurface[cell(b)],waveSurface[cell(b+vec2<i32>(1,0))],f.x),
   mix(waveSurface[cell(b+vec2<i32>(0,1))],waveSurface[cell(b+vec2<i32>(1,1))],f.x),f.y);
}
fn particle_flow(pos:vec2<f32>)->vec2<f32> {
 let angle=pos*1.5707963;let wave=pos*2.0+vec2<f32>(u.grid.w*0.2,-u.grid.w*0.16);
 let modulation=1.0+0.35*sin(wave.x)*sin(wave.y);
 return vec2<f32>(cos(angle.x)*(-1.5707963*sin(angle.y)*modulation+cos(angle.y)*0.7*sin(wave.x)*cos(wave.y)),
   cos(angle.y)*(1.5707963*sin(angle.x)*modulation-cos(angle.x)*0.7*cos(wave.x)*sin(wave.y)))*0.045*${PARCEL_POLICY.fieldSpeed}.0;
}
fn velocity_at(pos:vec2<f32>)->vec2<f32> {
 let delta=2.0/u.grid.x;
 let gradient=vec2<f32>(wave_at(pos+vec2<f32>(delta,0)).x-wave_at(pos-vec2<f32>(delta,0)).x,
   wave_at(pos+vec2<f32>(0,delta)).x-wave_at(pos-vec2<f32>(0,delta)).x)*u.grid.x*0.025;
 let filmVelocity=vec2<f32>(-gradient.y,gradient.x);
 var v=flow_at(pos).xy*2.0*u.theme.x+filmVelocity*u.theme.y+particle_flow(pos)*u.theme.z;
 let pointerDelta=u.pointer.xy-pos;
 let force=u.pointer.z/max(dot(pointerDelta,pointerDelta),0.018);
 v+=pointerDelta*force*0.012*(u.theme.y+u.theme.z);
 // The outer boundary is circular and outside every supported viewport, not a visible box.
 v-=pos/max(length(pos),0.001)*smoothstep(1.45,${PARCEL_POLICY.worldEdge},length(pos))*0.35;
 return v/max(1.0,length(v)/${PARCEL_POLICY.speedLimit});
}
fn spectral(phase:f32)->vec3<f32> { return 0.5+0.5*cos(vec3<f32>(0,2,4)+phase*6.28318); }
@compute @workgroup_size(${PARCEL_POLICY.workgroup})
fn main(@builtin(global_invocation_id) gid:vec3<u32>) {
 let i=gid.x;if(i>=arrayLength(&src)){return;}
 var p=src[i];let dt=u.grid.z;let shape=u.theme.w;let live=1.0-shape;
 let hash=(i*1664525u+1013904223u)^(i>>9u);
 let v0=velocity_at(p.pos);let midpoint=p.pos+v0*(dt*live*0.5);
 var targetVelocity=velocity_at(midpoint);
 if(u.burst.z>0.0){
  let away=p.pos-u.burst.xy;let distance=length(away);
  // Coincident gathered parcels need distinct directions when the magnet releases.
  let angle=f32(hash&65535u)/65535.0*6.2831853;
  let direction=select(vec2<f32>(cos(angle),sin(angle)),away/max(distance,0.00001),distance>0.005);
  targetVelocity+=direction*u.burst.z*(1.0-smoothstep(0.0,u.burst.w,distance));
  targetVelocity/=max(1.0,length(targetVelocity)/${PARCEL_POLICY.speedLimit});
 }
 let response=1.0-exp(-${PARCEL_POLICY.velocityResponse}.0*dt);
 p.vel=mix(p.vel,targetVelocity,response)*exp(-${PARCEL_POLICY.settleDamping}.0*dt*shape);
 p.pos+=targetVelocity*(dt*live);
 p.pos/=max(1.0,length(p.pos)/${PARCEL_POLICY.worldEdge});
 let goal=goals[hash%arrayLength(&goals)].xy;
 // Different parcel arrival rates break the rectangular envelope of a uniform zoom.
 let arrival=0.35+1.65*f32((hash>>16u)&65535u)/65535.0;
 // Gathering follows visible formation time, independently of the fluid stability timestep.
 let recovery=1.0-exp(-${PARCEL_POLICY.settleRate}.0*u.pointer.w*arrival*shape);
 p.pos=mix(p.pos,goal,recovery);
 let index=cell(vec2<i32>((p.pos*0.5+0.5)*u.grid.x));
 let dye=clamp(flowDye[index].rgb,vec3<f32>(0),vec3<f32>(8));
 let flow=flow_at(p.pos);let film=wave_at(p.pos);let curl=flowCurl[index].y;
 let reflection=log(1.0+max(film.w,0.0));
 let waveColor=spectral(film.x*0.34+reflection*0.14);
 let wantedColor=mix(p.color.rgb,dye,0.12)*u.theme.x+waveColor*u.theme.y+p.color.rgb*u.theme.z;
 let wantedMaterial=vec4<f32>((dot(dye,vec3<f32>(0.333))+length(flow.xy)*0.18)*u.theme.x+film.x*u.theme.y,
   reflection,sign(curl)*log(1.0+abs(curl)),length(p.vel));
 let colorResponse=(1.0-exp(-${PARCEL_POLICY.colorResponse}.0*dt))*live;
 p.color=vec4<f32>(mix(p.color.rgb,wantedColor,colorResponse),1);
 p.material=clamp(mix(p.material,wantedMaterial,colorResponse),vec4<f32>(-8),vec4<f32>(8));
 dst[i]=p;
}
`;
function createParcelData(grid, seed, count = grid * grid) {
  const data = new Float32Array(count * PARCEL_POLICY.strideFloats);
  for (let i = 0; i < count; i++) {
    // A uniform disc with an offscreen boundary avoids showing a rectangular grid edge.
    const angle = i * Math.PI * (3 - Math.sqrt(5));
    const radius = Math.sqrt((i + 0.5) / count) * PARCEL_POLICY.initialRadius;
    const x = Math.cos(angle) * radius,
      y = Math.sin(angle) * radius;
    const sx = (((x * 0.5 + 0.5) % 1) + 1) % 1,
      sy = (((y * 0.5 + 0.5) % 1) + 1) % 1;
    const cell = Math.floor(sy * grid) * grid + Math.floor(sx * grid);
    const color = Array.from(seed.dye.slice(cell * 4, cell * 4 + 3));
    const v = Array.from(seed.velocity.slice(cell * 4, cell * 4 + 2));
    const height = (color[0] + color[1] + color[2]) / 3 + Math.hypot(...v) * 0.18;
    data.set(
      [x, y, ...v.map((n) => n * 2), ...color, 1, height, 0, 0, Math.hypot(...v) * 2],
      i * PARCEL_POLICY.strideFloats,
    );
  }
  return data;
}
export { PARCEL_POLICY, PARCEL_UPDATE, createParcelData };
