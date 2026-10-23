fn probe(a:vec2<i32>,b:vec2<i32>,c:vec2<i32>) -> vec2<i32> {return clamp(a,b,c);}
@compute @workgroup_size(1) fn main(){}
