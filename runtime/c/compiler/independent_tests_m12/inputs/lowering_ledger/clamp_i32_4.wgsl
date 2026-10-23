fn probe(a:vec4<i32>,b:vec4<i32>,c:vec4<i32>) -> vec4<i32> {return clamp(a,b,c);}
@compute @workgroup_size(1) fn main(){}
