fn probe(a:u32,b:u32,c:u32) -> u32 {return clamp(a,b,c);}
@compute @workgroup_size(1) fn main(){}
