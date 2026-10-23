fn dot(a:vec2f,b:vec2f)->f32{return a.x+b.x;}@compute @workgroup_size(1) fn main(){let result=dot<f32>(vec2f(),vec2f());}
