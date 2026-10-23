fn probe(a:vec4<f32>,b:vec4<f32>) -> vec4<f32> {return min(a,b);}
@compute @workgroup_size(1) fn main(){}
