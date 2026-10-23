fn probe(a:vec4<f32>) -> vec4<f32> {return sqrt(a);}
@compute @workgroup_size(1) fn main(){}
