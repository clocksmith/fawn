fn probe(a:vec3<f32>,b:vec3<f32>,c:vec3<f32>) -> vec3<f32> {return clamp(a,b,c);}
@compute @workgroup_size(1) fn main(){}
