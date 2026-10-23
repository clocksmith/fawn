fn probe(a:vec4<i32>,b:vec4<i32>) -> i32 {return dot(a,b);}
@compute @workgroup_size(1) fn main(){}
