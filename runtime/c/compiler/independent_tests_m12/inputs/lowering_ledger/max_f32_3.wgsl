fn probe(a:vec3<f32>,b:vec3<f32>) -> vec3<f32> {return max(a,b);}
@compute @workgroup_size(1) fn main(){}
