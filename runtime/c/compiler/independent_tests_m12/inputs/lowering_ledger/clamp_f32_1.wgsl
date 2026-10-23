fn probe(a:f32,b:f32,c:f32) -> f32 {return clamp(a,b,c);}
@compute @workgroup_size(1) fn main(){}
