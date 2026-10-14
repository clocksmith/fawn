// Persistent moving material. Formation steers momentum, never projects positions onto a mask.
const PARCEL_POLICY = Object.freeze({
  version: 11,
  strideFloats: 16,
  workgroup: 128,
  targetGrid: 384,
  speedLimit: 1.75,
  fieldSpeed: 12,
  velocityResponse: 12,
  particleVelocityResponse: 2,
  particleDriftGain: 0.6,
  particleFormationGain: 4,
  particleWander: 0.012,
  particleReleaseSpring: 0.35,
  particleReleaseDecay: 1,
  colorResponse: 3,
  formationSpring: 28,
  formationSwirl: 0.6,
  insidePull: 0.04,
  formationCirculation: 0.15,
  formationPigmentRetention: 0.85,
  formationDrag: 0.8,
  formationFieldResponse: 8,
  boundarySpring: 128,
  boundaryReach: 0.08,
  releaseSpring: 9,
  releaseResponse: 2,
  releaseDistance: 0.12,
  pressSwirl: 7,
  pressReach: 0.35,
  boundaryBand: 0.025,
  particleBoundaryBand: 0.08,
  worldEdge: 1.65,
  initialRadius: 1.65,
});
const PARCEL_STRUCT = `struct Parcel {
 pos:vec2<f32>, vel:vec2<f32>, color:vec4<f32>, material:vec4<f32>,
 home:vec2<f32>, formation:vec2<f32>
}`;
const PARCEL_UPDATE = `
${PARCEL_STRUCT}
struct Params { grid: vec4<f32>, theme: vec4<f32>, pointer: vec4<f32>, burst: vec4<f32>, view: vec4<f32> }
@group(0) @binding(0) var<uniform> u: Params;
@group(0) @binding(1) var<storage,read> src: array<Parcel>;
@group(0) @binding(2) var<storage,read_write> dst: array<Parcel>;
@group(0) @binding(3) var<storage,read> goals: array<vec4<f32>>;
@group(0) @binding(4) var<storage,read> flowVelocity: array<vec4<f32>>;
@group(0) @binding(5) var<storage,read> flowDye: array<vec4<f32>>;
@group(0) @binding(6) var<storage,read> waveSurface: array<vec4<f32>>;
@group(0) @binding(7) var<storage,read> flowCurl: array<vec4<f32>>;
@group(0) @binding(8) var<storage,read> logoBoundary: array<vec4<f32>>;
fn mark_at(pos:vec2<f32>)->vec4<f32> {
 let size=${PARCEL_POLICY.targetGrid}.0;
 let p=clamp((pos*0.5+0.5)*size-0.5,vec2<f32>(0),vec2<f32>(size-1));
 let b=vec2<u32>(floor(p));let f=fract(p);let edge=u32(size)-1u;
 let q=min(b+vec2<u32>(1),vec2<u32>(edge));
 let mark=mix(mix(logoBoundary[b.y*u32(size)+b.x],logoBoundary[b.y*u32(size)+q.x],f.x),
   mix(logoBoundary[q.y*u32(size)+b.x],logoBoundary[q.y*u32(size)+q.x],f.x),f.y);
 return vec4<f32>(mark.xyz,mark.w*2.0);
}
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
// The standalone particle page's galaxy in screen-normalized coordinates, with a firmer inward
// pull so it stays a spiralling disc, and soft screen walls; returned as a world velocity.
fn particle_spiral(pos:vec2<f32>)->vec2<f32> {
 let q=pos/u.view.xy;let radius=max(length(q),0.001);let orbit=vec2<f32>(-q.y,q.x)/radius;
 var field=orbit*(0.020+0.034*(1.0-smoothstep(0.18,1.05,radius)))
   -q/radius*(0.014+0.03*(1.0-smoothstep(0.24,1.08,radius)));
 field-=sign(q)*smoothstep(vec2<f32>(0.80),vec2<f32>(0.98),abs(q))*0.09;
 return field/0.024*u.view.xy;
}
fn velocity_at(pos:vec2<f32>)->vec2<f32> {
 var v=vec2<f32>(0);
 // Particles leave their own field as they form the mark, as the standalone page did.
 let free=u.theme.z*(1.0-u.theme.w);
 if(u.theme.x>0.0){v+=flow_at(pos).xy*2.0*u.theme.x;}
 if(u.theme.y>0.0){
  let delta=2.0/u.grid.x;
  let gradient=vec2<f32>(wave_at(pos+vec2<f32>(delta,0)).x-wave_at(pos-vec2<f32>(delta,0)).x,
    wave_at(pos+vec2<f32>(0,delta)).x-wave_at(pos-vec2<f32>(0,delta)).x)*u.grid.x*0.025;
  v+=vec2<f32>(-gradient.y,gradient.x)*u.theme.y;
 }
 let drift=${PARCEL_POLICY.particleDriftGain}*free
   +${PARCEL_POLICY.formationCirculation}*u.theme.w*u.theme.x;
 if(drift>0.0){v+=particle_flow(pos)*drift;}
 if(free>0.0){v+=particle_spiral(pos)*free;}
 let pointerDelta=u.pointer.xy-pos;
 let reach=0.012/max(dot(pointerDelta,pointerDelta),0.018);
 // Magnets attract and Particles repel; where they cancel mid-blend a held press swirls instead.
 let swirl=u.pointer.w*min(u.theme.y,u.theme.z)*${PARCEL_POLICY.pressSwirl}.0;
 v+=(pointerDelta*u.pointer.z*(u.theme.y+u.theme.z)+vec2<f32>(-pointerDelta.y,pointerDelta.x)*swirl)*reach;
 // The outer boundary is circular and outside every supported viewport, not a visible box.
 v-=pos/max(length(pos),0.001)*smoothstep(1.45,${PARCEL_POLICY.worldEdge},length(pos))*0.35;
 return v/max(1.0,length(v)/${PARCEL_POLICY.speedLimit});
}
fn spectral(phase:f32)->vec3<f32> { return 0.5+0.5*cos(vec3<f32>(0,2,4)+phase*6.28318); }
@compute @workgroup_size(${PARCEL_POLICY.workgroup})
fn main(@builtin(global_invocation_id) gid:vec3<u32>) {
 let i=gid.x;if(i>=arrayLength(&src)){return;}
 var p=src[i];let dt=u.grid.z;let shape=u.theme.w;
 let hash=(i*1664525u+1013904223u)^(i>>9u);
 let v0=velocity_at(p.pos);let midpoint=p.pos+v0*(dt*0.5);
 var targetVelocity=velocity_at(midpoint);
 targetVelocity/=max(1.0,length(targetVelocity)/${PARCEL_POLICY.speedLimit});
 if(u.burst.z>0.0){
  let away=p.pos-u.burst.xy;let distance=length(away);
  // Coincident gathered parcels need distinct directions when the magnet releases.
  let angle=f32(hash&65535u)/65535.0*6.2831853;
  let direction=select(vec2<f32>(cos(angle),sin(angle)),away/max(distance,0.00001),distance>0.005);
  targetVelocity+=direction*u.burst.z*(1.0-smoothstep(0.0,u.burst.w,distance));
  targetVelocity/=max(1.0,length(targetVelocity)/${PARCEL_POLICY.speedLimit});
 }
 // The outline is a soft restoring force: material may cross it and flow back.
 var before=vec4<f32>(0,0,0,1);
 if(shape>0.0){before=mark_at(p.pos);}
 let inward=before.xy/max(length(before.xy),0.000001);
 let velocityResponse=mix(${PARCEL_POLICY.velocityResponse}.0,${PARCEL_POLICY.particleVelocityResponse},u.theme.z);
 let site=goals[hash%arrayLength(&goals)];
 // Formed particles hover around their places so the mark stays a living cloud.
 let wanderPhase=f32(hash&65535u)/65535.0*6.2831853;let wanderRate=0.8*(0.7+f32(i%17u)/17.0);
 let wanderRadius=min(${PARCEL_POLICY.particleWander},max(0.0,site.z-2.0/${PARCEL_POLICY.targetGrid}.0)*0.55);
 let goal=site.xy+vec2<f32>(cos(u.grid.w*wanderRate+wanderPhase),sin(u.grid.w*wanderRate+wanderPhase))
   *wanderRadius*u.theme.z;
 let arrival=0.35+1.65*f32((hash>>16u)&65535u)/65535.0;
 let boundaryBand=mix(${PARCEL_POLICY.boundaryBand},${PARCEL_POLICY.particleBoundaryBand},u.theme.z);
 let inside=smoothstep(-boundaryBand,0.0,before.w);
 // Fluid inside the mark floats; particles still take their own places so the mark fills.
 let pull=mix(1.0,${PARCEL_POLICY.insidePull},inside*(1.0-u.theme.z));
 // Remember release until the material has spread; a zero shape must not leave it clumped.
 let previous=p.formation.x;
 var release=min(1.0,p.formation.y+max(0.0,previous-shape));
 let homeDistance=length(p.home-p.pos);
 let arrived=1.0-smoothstep(0.0,${PARCEL_POLICY.releaseDistance},homeDistance);
 release*=exp(-dt*(${PARCEL_POLICY.releaseResponse}.0*(arrived+shape)
   +${PARCEL_POLICY.particleReleaseDecay}.0*u.theme.z));
 let releaseWeight=release*(1.0-shape);
 // Material under a held press follows the hand; formation and return resume on release.
 let held=u.pointer.w*(1.0-smoothstep(0.0,${PARCEL_POLICY.pressReach},length(u.pointer.xy-p.pos)));
 // Particles retain their free-flight momentum longer before the gather force builds.
 let gather=shape*mix(1.0,shape,u.theme.z)*(1.0-held);
 let spring=${PARCEL_POLICY.formationSpring}.0*arrival*gather*pull
   *mix(1.0,${PARCEL_POLICY.particleFormationGain}.0,u.theme.z);
 // Material spirals in while the live field keeps carrying it, rather than taking straight lines.
 let toGoal=goal-p.pos;
 // The outline only catches material that has reached it; distant material is not squeezed inward.
 let edge=max(0.0,-before.w)*(1.0-smoothstep(0.0,${PARCEL_POLICY.boundaryReach},-before.w));
 // Released particles disperse on their own momentum and field instead of retracing home.
 let spread=${PARCEL_POLICY.releaseSpring}.0*releaseWeight*(1.0-held)
   *mix(1.0,${PARCEL_POLICY.particleReleaseSpring},u.theme.z);
 let fieldResponse=mix(velocityResponse,${PARCEL_POLICY.formationFieldResponse}.0,gather*(1.0-inside));
 let drag=fieldResponse+${PARCEL_POLICY.formationDrag}*2.0*sqrt(spring+spread);
 let force=targetVelocity*fieldResponse
   +(toGoal+vec2<f32>(-toGoal.y,toGoal.x)*${PARCEL_POLICY.formationSwirl}*(1.0-inside))*spring
   +(p.home-p.pos)*spread+inward*edge*${PARCEL_POLICY.boundarySpring}.0*gather;
 // Integrate a damped force through the retained velocity; no shape-dependent position edits.
 p.vel=mix(p.vel,force/max(drag,0.000001),1.0-exp(-drag*dt));
 p.vel/=max(1.0,length(p.vel)/${PARCEL_POLICY.speedLimit});
 p.pos+=p.vel*dt;
 p.formation=vec2<f32>(shape,release);
 let index=cell(vec2<i32>((p.pos*0.5+0.5)*u.grid.x));
 var dye=vec3<f32>(0);var flow=vec4<f32>(0);var film=vec4<f32>(0);var curl=0.0;
 if(u.theme.x>0.0){
  dye=clamp(flowDye[index].rgb,vec3<f32>(0),vec3<f32>(8));
  flow=flow_at(p.pos);curl=flowCurl[index].y;
 }
 var tilt=vec2<f32>(0);
 if(u.theme.y>0.0){
  film=wave_at(p.pos);let d=2.0/u.grid.x;
  tilt=-vec2<f32>(wave_at(p.pos+vec2<f32>(d,0)).x-wave_at(p.pos-vec2<f32>(d,0)).x,
    wave_at(p.pos+vec2<f32>(0,d)).x-wave_at(p.pos-vec2<f32>(0,d)).x)*u.grid.x*0.06;
 }
 let reflection=log(1.0+max(film.w,0.0));
 // Carry the iridescence the film shows here, so released particles keep its rainbow.
 let normal=normalize(vec3<f32>(tilt,1));
 let waveColor=spectral(film.x*0.34+reflection*0.14+pow(1.0-normal.z,3.0)*0.55
   +dot(normal.xy,vec2<f32>(0.4,-0.3))+u.grid.w*0.015);
 // Gathered fluid carries its colors instead of repainting every parcel from one small region.
 let carriedDye=mix(dye,p.color.rgb,shape*${PARCEL_POLICY.formationPigmentRetention});
 let carriedFilm=mix(waveColor,p.color.rgb,shape*${PARCEL_POLICY.formationPigmentRetention});
 // The particle mark takes the original particle page's positional palette as it forms.
 let palette=vec3<f32>(0.55,0.64,0.63)+vec3<f32>(0.34,0.27,0.32)
   *cos(vec3<f32>(0.0,2.1,4.2)+(goal.x*0.65+goal.y*0.35)*3.14159);
 let particleColor=mix(p.color.rgb,palette,shape);
 let wantedColor=carriedDye*u.theme.x+carriedFilm*u.theme.y+particleColor*u.theme.z;
 let wantedMaterial=vec4<f32>((dot(dye,vec3<f32>(0.333))+length(flow.xy)*0.18)*u.theme.x+film.x*u.theme.y,
   reflection,sign(curl)*log(1.0+abs(curl)),length(p.vel));
 let colorResponse=1.0-exp(-${PARCEL_POLICY.colorResponse}.0*dt);
 p.color=vec4<f32>(mix(p.color.rgb,wantedColor,colorResponse),1.0);
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
      [x, y, ...v.map((n) => n * 2), ...color, 1, height, 0, 0, Math.hypot(...v) * 2, x, y, 0, 0],
      i * PARCEL_POLICY.strideFloats,
    );
  }
  return data;
}
export { PARCEL_POLICY, PARCEL_STRUCT, PARCEL_UPDATE, createParcelData };
