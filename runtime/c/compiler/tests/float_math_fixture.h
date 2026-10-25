/* SPDX-License-Identifier: MIT */
#ifndef DOE_TEST_FLOAT_MATH_FIXTURE_H
#define DOE_TEST_FLOAT_MATH_FIXTURE_H
static const char float_math_module[] =
    "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
    "fn changed(slot:u32,tag:u32,value:f32)->f32{data[slot]=data[slot]*10u+tag;return value;}"
    "@compute @workgroup_size(1) fn main(){"
    "var a=vec4f(1,2,-3,0.5);var b=vec4f(2,3,4,8);var c=vec4f(0.5,-1,2,-3);"
    "let f=fma(a,b,c);data[0]=bitcast<u32>(f.x);data[1]=bitcast<u32>(f.y);"
    "data[2]=bitcast<u32>(f.z);data[3]=bitcast<u32>(f.w);"
    "var edge=vec4f(0.5,0.5,-1,1);var x=vec4f(0.25,0.5,0,0);let s=step(edge,x);"
    "data[4]=bitcast<u32>(s.x);data[5]=bitcast<u32>(s.y);data[6]=bitcast<u32>(s.z);data[7]=bitcast<u32>(s.w);"
    "var v=vec4f(-2,-0.0,0.25,2);let t=saturate(v);"
    "data[8]=bitcast<u32>(t.x);data[9]=bitcast<u32>(t.y);data[10]=bitcast<u32>(t.z);data[11]=bitcast<u32>(t.w);"
    "var values=vec4f(-1,0.25,0.5,2);let up=smoothstep(vec4f(0),vec4f(1),values);"
    "data[12]=bitcast<u32>(up.x);data[13]=bitcast<u32>(up.y);data[14]=bitcast<u32>(up.z);data[15]=bitcast<u32>(up.w);"
    "let down=smoothstep(vec4f(1),vec4f(0),values);"
    "data[16]=bitcast<u32>(down.x);data[17]=bitcast<u32>(down.y);data[18]=bitcast<u32>(down.z);data[19]=bitcast<u32>(down.w);"
    "const cf=fma(2,3,-1);const cs=step(0.5,0.5);const ct=saturate(1.5);const cu=smoothstep(1.,0.,0.25);"
    "data[20]=bitcast<u32>(f32(cf));data[21]=bitcast<u32>(f32(cs));"
    "data[22]=bitcast<u32>(f32(ct));data[23]=bitcast<u32>(f32(cu));"
    "data[24]=0u;data[25]=bitcast<u32>(fma(changed(24u,1u,2),changed(24u,2u,3),changed(24u,3u,4)));"
    "data[26]=0u;data[27]=bitcast<u32>(smoothstep(changed(26u,1u,0),changed(26u,2u,1),changed(26u,3u,0.5)));"
    "data[28]=0u;data[29]=bitcast<u32>(step(changed(28u,1u,0.5),changed(28u,2u,0.25)));"
    "data[30]=0u;data[31]=bitcast<u32>(saturate(changed(30u,1u,2)));"
    "const finite=fma(3.402823466e38f,2.0f,-3.402823466e38f);data[32]=bitcast<u32>(finite);"
    "}";
#endif
