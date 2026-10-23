struct dot { value:f32, }@compute @workgroup_size(1) fn main(){let result=dot<f32>(1.0f);}
