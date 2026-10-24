@group(0) @binding(0) var<storage,read_write> data:array<u32>;

fn mark(value:u32)->u32 {
    data[28]=data[28]*10u+value;
    return value;
}
fn condition()->bool {
    data[28]=data[28]*10u+3u;
    return true;
}
fn change()->u32 {
    data[29]=5u;
    return 7u;
}
@compute @workgroup_size(1) fn main() {
    let c=data[31]!=0u;
    data[0]=select(11u,29u,c);
    let scalar_mask=select(vec4u(2u,4u,6u,8u),vec4u(13u,17u,19u,23u),c);
    data[1]=scalar_mask.x;
    data[2]=scalar_mask.y;
    data[3]=scalar_mask.z;
    data[4]=scalar_mask.w;
    let vector_mask=select(vec4u(2u,4u,6u,8u),vec4u(13u,17u,19u,23u),vec4<bool>(c,!c,c,!c));
    data[5]=vector_mask.x;
    data[6]=vector_mask.y;
    data[7]=vector_mask.z;
    data[8]=vector_mask.w;
    let signed_values=select(vec2i(-7,-11),vec2i(31,37),vec2<bool>(!c,c));
    data[9]=bitcast<u32>(signed_values.x);
    data[10]=bitcast<u32>(signed_values.y);
    let float_values=select(vec2f(-0.0,1.5),vec2f(7.25,-2.5),vec2<bool>(c,!c));
    data[11]=bitcast<u32>(float_values.x);
    data[12]=bitcast<u32>(float_values.y);
    let bool_values=select(vec3<bool>(false,true,false),vec3<bool>(true,false,true),vec3<bool>(c,!c,!c));
    data[13]=select(0u,1u,bool_values.x);
    data[14]=select(0u,1u,bool_values.y);
    data[15]=select(0u,1u,bool_values.z);
    data[16]=select(41u,43u,select(false,true,c));
    data[17]=select(mark(1u),mark(2u),condition());
    data[18]=data[28];
    data[19]=select(data[29],change(),!c);
    data[20]=data[29];
    const const_values=select(vec3u(2u,4u,6u),vec3u(13u,17u,19u),vec3<bool>(true,false,true));
    data[21]=const_values.x;
    data[22]=const_values.y;
    data[23]=const_values.z;
}
