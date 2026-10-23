@compute @workgroup_size(1) fn main(){let result=false && (clamp(vec4<i32>(2i,2i,2i,2i),vec4<i32>(1i,5i,1i,1i),vec4<i32>(3i,3i,3i,3i)).x==2i);}
