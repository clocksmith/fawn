fn probe(a:vec3<i32>,b:vec3<i32>) -> vec3<i32> {return max(a,b);}
@compute @workgroup_size(1) fn main(){}
