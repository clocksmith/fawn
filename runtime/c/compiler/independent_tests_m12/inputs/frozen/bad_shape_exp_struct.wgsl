struct Box{x:vec3f};@compute @workgroup_size(1) fn main(){let result=exp(Box(vec3f(1.0f)));}
