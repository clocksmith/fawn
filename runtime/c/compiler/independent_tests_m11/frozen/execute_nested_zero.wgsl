@group(0) @binding(0) var<storage,read_write> out:array<u32>;
struct S{v:vec2u,b:bool,f:f32}; var<workgroup> a:array<S,4>;@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32,@builtin(local_invocation_id) lid:vec3u,@builtin(global_invocation_id) gid:vec3u,@builtin(workgroup_id) wid:vec3u,@builtin(num_workgroups) nw:vec3u){
out[gid.x]=a[li].v.x+a[li].v.y+u32(a[li].b)+u32(a[li].f);
}
