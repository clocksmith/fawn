// GPU handoffs preserve source coordinates, pigment, and the moving field. No CPU readback.
import { PARCEL_STRUCT } from './fawn-parcels.js';
const TRANSFER_POLICY = Object.freeze({
  version: 1,
  fixedScale: 1024,
  particleVelocityLimit: 0.18,
  pigmentFloor: 0.004,
  particleBrightness: 1.0,
});
const HEADER = `
struct Params { grid: vec4<f32>, transport: vec4<f32>, bounds: vec4<f32> }
@group(0) @binding(0) var<uniform> u: Params;
fn cell(p: vec2<i32>) -> u32 {
  let q = clamp(p, vec2<i32>(0), vec2<i32>(i32(u.grid.x)-1));
  return u32(q.y) * u32(u.grid.x) + u32(q.x);
}
`;
const FLOW_TO_WAVE =
  HEADER +
  `
@group(0) @binding(1) var<storage,read> dye: array<vec4<f32>>;
@group(0) @binding(2) var<storage,read> velocity: array<vec4<f32>>;
@group(0) @binding(3) var<storage,read_write> surface: array<vec4<f32>>;
@compute @workgroup_size(128) fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
 let i=gid.x; if(i>=u32(u.grid.x*u.grid.y)||i>=arrayLength(&dye)){return;}
 let color=dye[i].rgb; let brightness=dot(color,vec3<f32>(0.2126,0.7152,0.0722));
 surface[i]=vec4<f32>(clamp(brightness*0.65,0.0,1.6),length(velocity[i].xy)*0.08,0.0,brightness*12.0);
}
`;
const PARTICLE_DEPOSIT =
  HEADER +
  `
${PARCEL_STRUCT}
struct Deposit { weight: atomic<i32>, r: atomic<i32>, g: atomic<i32>, b: atomic<i32>, vx: atomic<i32>, vy: atomic<i32> }
@group(0) @binding(1) var<storage,read> particles: array<Parcel>;
@group(0) @binding(2) var<storage,read_write> deposits: array<Deposit>;
@compute @workgroup_size(128) fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
 let i=gid.x; if(i>=arrayLength(&particles)){return;}
 let p=particles[i]; let position=(p.pos*0.5+0.5)*u.grid.x-0.5; let base=vec2<i32>(floor(position)); let f=fract(position);
 for(var y=0;y<2;y++){for(var x=0;x<2;x++){
  let weight=select(1.0-f.x,f.x,x==1)*select(1.0-f.y,f.y,y==1);
  let amount=weight*${TRANSFER_POLICY.fixedScale}.0;let index=cell(base+vec2<i32>(x,y));
  atomicAdd(&deposits[index].weight,i32(amount));
  atomicAdd(&deposits[index].r,i32(clamp(p.color.r,0.0,4.0)*amount));
  atomicAdd(&deposits[index].g,i32(clamp(p.color.g,0.0,4.0)*amount));
  atomicAdd(&deposits[index].b,i32(clamp(p.color.b,0.0,4.0)*amount));
  atomicAdd(&deposits[index].vx,i32(clamp(p.vel.x,-1.75,1.75)*amount));
  atomicAdd(&deposits[index].vy,i32(clamp(p.vel.y,-1.75,1.75)*amount));
 }}
}
`;
const RESOLVE_DEPOSIT =
  HEADER +
  `
struct Deposit { weight: i32, r: i32, g: i32, b: i32, vx: i32, vy: i32 }
@group(0) @binding(1) var<storage,read> deposits: array<Deposit>;
@group(0) @binding(2) var<storage,read_write> dye: array<vec4<f32>>;
@group(0) @binding(3) var<storage,read_write> velocity: array<vec4<f32>>;
@compute @workgroup_size(128) fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
 let i=gid.x;if(i>=u32(u.grid.x*u.grid.y)||i>=arrayLength(&dye)){return;}
 let d=deposits[i];let weight=max(f32(d.weight),1.0);
 let density=min(1.0,f32(d.weight)/${TRANSFER_POLICY.fixedScale}.0);
 dye[i]=vec4<f32>(vec3<f32>(f32(d.r),f32(d.g),f32(d.b))/weight*density,1.0);
 velocity[i]=vec4<f32>(vec2<f32>(f32(d.vx),f32(d.vy))/weight*0.5,0.0,0.0);
}
`;
export { TRANSFER_POLICY, FLOW_TO_WAVE, PARTICLE_DEPOSIT, RESOLVE_DEPOSIT };
