@compute @workgroup_size(1) fn main(){let result=false && (clamp(vec4<f32>(2.0f,2.0f,2.0f,2.0f),vec4<f32>(1.0f,1.0f,5.0f,1.0f),vec4<f32>(3.0f,3.0f,3.0f,3.0f)).x==2.0f);}
