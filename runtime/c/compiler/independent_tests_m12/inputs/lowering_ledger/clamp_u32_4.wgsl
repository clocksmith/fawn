fn probe(a:vec4<u32>,b:vec4<u32>,c:vec4<u32>) -> vec4<u32> {return clamp(a,b,c);}
@compute @workgroup_size(1) fn main(){}
