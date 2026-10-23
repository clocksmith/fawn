fn probe(a:vec2<f32>,b:vec2<f32>) -> vec2<f32> {return max(a,b);}
@compute @workgroup_size(1) fn main(){}
