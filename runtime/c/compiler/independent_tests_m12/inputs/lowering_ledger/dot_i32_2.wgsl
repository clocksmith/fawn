fn probe(a:vec2<i32>,b:vec2<i32>) -> i32 {return dot(a,b);}
@compute @workgroup_size(1) fn main(){}
