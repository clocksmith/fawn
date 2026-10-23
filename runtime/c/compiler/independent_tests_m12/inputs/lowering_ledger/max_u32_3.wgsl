fn probe(a:vec3<u32>,b:vec3<u32>) -> vec3<u32> {return max(a,b);}
@compute @workgroup_size(1) fn main(){}
