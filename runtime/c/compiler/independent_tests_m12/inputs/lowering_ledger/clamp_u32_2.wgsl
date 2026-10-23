fn probe(a:vec2<u32>,b:vec2<u32>,c:vec2<u32>) -> vec2<u32> {return clamp(a,b,c);}
@compute @workgroup_size(1) fn main(){}
