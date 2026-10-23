@compute @workgroup_size(1) fn main(){let result=false && (clamp(vec3<i32>(2i,2i,2i),vec3<i32>(1i,1i,5i),vec3<i32>(3i,3i,3i)).x==2i);}
