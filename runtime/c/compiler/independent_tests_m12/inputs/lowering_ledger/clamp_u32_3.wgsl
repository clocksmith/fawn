fn probe(a:vec3<u32>,b:vec3<u32>,c:vec3<u32>) -> vec3<u32> {return clamp(a,b,c);}
@compute @workgroup_size(1) fn main(){}
