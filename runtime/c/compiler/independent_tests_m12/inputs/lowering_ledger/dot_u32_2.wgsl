fn probe(a:vec2<u32>,b:vec2<u32>) -> u32 {return dot(a,b);}
@compute @workgroup_size(1) fn main(){}
