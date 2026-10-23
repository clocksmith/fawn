fn probe(a:vec4<f32>) -> f32 {return length(a);}
@compute @workgroup_size(1) fn main(){}
