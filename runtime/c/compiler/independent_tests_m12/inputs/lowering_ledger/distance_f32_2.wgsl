fn probe(a:vec2<f32>,b:vec2<f32>) -> f32 {return distance(a,b);}
@compute @workgroup_size(1) fn main(){}
