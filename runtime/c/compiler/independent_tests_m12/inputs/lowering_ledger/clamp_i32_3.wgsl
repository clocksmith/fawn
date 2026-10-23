fn probe(a:vec3<i32>,b:vec3<i32>,c:vec3<i32>) -> vec3<i32> {return clamp(a,b,c);}
@compute @workgroup_size(1) fn main(){}
