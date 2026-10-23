fn probe(a:vec2<f32>) -> vec2<f32> {return tanh(a);}
@compute @workgroup_size(1) fn main(){}
