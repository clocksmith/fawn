@compute @workgroup_size(1) fn main(){let result=false && (clamp(vec3<u32>(2u,2u,2u),vec3<u32>(5u,1u,1u),vec3<u32>(3u,3u,3u)).x==2u);}
