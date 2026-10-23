fn probe(a:vec2<u32>,b:vec2<u32>) -> vec2<u32> {return max(a,b);}
@compute @workgroup_size(1) fn main(){}
