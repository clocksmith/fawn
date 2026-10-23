fn probe(a:vec3<u32>,b:vec3<u32>) -> u32 {return dot(a,b);}
@compute @workgroup_size(1) fn main(){}
