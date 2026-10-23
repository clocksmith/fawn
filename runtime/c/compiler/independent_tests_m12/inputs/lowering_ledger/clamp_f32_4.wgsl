fn probe(a:vec4<f32>,b:vec4<f32>,c:vec4<f32>) -> vec4<f32> {return clamp(a,b,c);}
@compute @workgroup_size(1) fn main(){}
