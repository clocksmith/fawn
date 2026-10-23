fn probe(a:f32) -> f32 {return tanh(a);}
@compute @workgroup_size(1) fn main(){}
