fn probe(a:vec4<u32>,b:vec4<u32>) -> vec4<u32> {return min(a,b);}
@compute @workgroup_size(1) fn main(){}
