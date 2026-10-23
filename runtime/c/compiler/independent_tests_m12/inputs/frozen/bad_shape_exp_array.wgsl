struct Box{x:vec3f};@compute @workgroup_size(1) fn main(){let result=exp(array<f32,2>(1.0f,2.0f));}
