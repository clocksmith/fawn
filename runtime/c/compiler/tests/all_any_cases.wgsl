// Binding0 contains exactly32 u32 words. Seed[28..31] = [flag,7,0,99], with flag0 or1.
// Literal result[0..27] for flag0, with buffer indices clamped (HLSL, SPIR-V):
// [0,1,0,1,0,0,0,0,0,1,1,0,0,1,0,1,1,0,1,1,5678,0,1234,1,5678,1,1,2].
// Words16/17/19 read data[32u] and data[0xffffffffu]: a clamp reads data[31]=99;
// the MSL guard reads zero, giving 0/1/0 there.
// flag1 changes only words4..7 and14 to1; the four seed words stay untouched.
@group(0) @binding(0) var<storage,read_write> data:array<u32>;
fn effect(tag:u32,value:bool)->bool {
    data[20]=data[20]*10u+tag;
    return value;
}
fn scalar()->bool {
    data[27]+=1u;
    return data[28]!=0u;
}
fn vector_mask()->vec3<bool> {
    data[25]+=1u;
    return vec3<bool>(true,false,true);
}
const a=all(vec4<bool>(true));
const b=any(vec3<bool>(false));
@compute @workgroup_size(1) fn main() {
    data[0]=select(0u,1u,all(false));
    data[1]=select(0u,1u,all(true));
    data[2]=select(0u,1u,any(false));
    data[3]=select(0u,1u,any(true));
    data[27]=0u;
    data[4]=select(0u,1u,all(scalar()));
    data[5]=select(0u,1u,any(scalar()));
    let flag=data[28]!=0u;
    data[6]=select(0u,1u,all(vec2<bool>(flag,true)));
    data[7]=select(0u,1u,any(vec2<bool>(flag,false)));
    data[8]=select(0u,1u,all(vec3<bool>(true,false,true)));
    data[9]=select(0u,1u,any(vec3<bool>(false,true,false)));
    data[10]=select(0u,1u,a);
    data[11]=select(0u,1u,b);
    let values=vec4u(data[29],data[30],data[31],data[28]);
    let mask=values>vec4u(0u);
    data[12]=select(0u,1u,all(mask));
    data[13]=select(0u,1u,any(mask));
    data[14]=select(0u,1u,any(mask.yw));
    data[15]=select(0u,1u,all(mask.xz));
    data[16]=select(0u,1u,all(vec2<bool>(data[32u]!=0u,data[29]!=0u)));
    data[17]=select(0u,1u,any(vec2<bool>(data[32u]==0u,false)));
    data[18]=select(0u,1u,all(vec2<bool>(any(vec2<bool>(false,true)),all(vec2<bool>(true)))));
    data[19]=select(0u,1u,any(data[0xffffffffu]!=0u));
    data[20]=0u;
    data[21]=select(0u,1u,all(vec4<bool>(effect(1u,false),effect(2u,true),effect(3u,true),effect(4u,true))));
    data[22]=data[20];
    data[20]=0u;
    data[23]=select(0u,1u,any(vec4<bool>(effect(5u,true),effect(6u,false),effect(7u,false),effect(8u,false))));
    data[24]=data[20];
    data[25]=0u;
    data[26]=select(0u,1u,any(vector_mask()));
}
