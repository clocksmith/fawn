struct Box{x:vec3f};@compute @workgroup_size(1) fn main(){let result=max(array<f32,2>(1.0f,2.0f),array<f32,2>(1.0f,2.0f));}
