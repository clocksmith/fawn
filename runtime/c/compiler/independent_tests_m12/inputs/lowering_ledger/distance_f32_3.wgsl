fn probe(a:vec3<f32>,b:vec3<f32>) -> f32 {return distance(a,b);}
@compute @workgroup_size(1) fn main(){}
