fn probe(a:vec4<f32>,b:vec4<f32>) -> f32 {return dot(a,b);}
@compute @workgroup_size(1) fn main(){}
