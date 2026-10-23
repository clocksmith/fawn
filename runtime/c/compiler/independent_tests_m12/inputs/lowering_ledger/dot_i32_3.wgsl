fn probe(a:vec3<i32>,b:vec3<i32>) -> i32 {return dot(a,b);}
@compute @workgroup_size(1) fn main(){}
