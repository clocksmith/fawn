@compute @workgroup_size(1) fn main(){let result=false && (clamp(vec2<f32>(2.0f,2.0f),vec2<f32>(1.0f,5.0f),vec2<f32>(3.0f,3.0f)).x==2.0f);}
