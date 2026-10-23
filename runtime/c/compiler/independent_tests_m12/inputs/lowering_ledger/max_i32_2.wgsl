fn probe(a:vec2<i32>,b:vec2<i32>) -> vec2<i32> {return max(a,b);}
@compute @workgroup_size(1) fn main(){}
