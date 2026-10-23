fn probe(a:i32,b:i32,c:i32) -> i32 {return clamp(a,b,c);}
@compute @workgroup_size(1) fn main(){}
