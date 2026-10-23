fn probe(a:vec4<i32>,b:vec4<i32>) -> vec4<i32> {return max(a,b);}
@compute @workgroup_size(1) fn main(){}
