fn probe(a:vec3<f32>) -> vec3<f32> {return tanh(a);}
@compute @workgroup_size(1) fn main(){}
