/* SPDX-License-Identifier: MIT */
#ifndef DOE_TEST_PACKED_INTEGER_FIXTURE_H
#define DOE_TEST_PACKED_INTEGER_FIXTURE_H

static const char packed_integer_module[] =
    "requires packed_4x8_integer_dot_product;"
    "const c0 = pack4xU8(vec4u(0u,255u,256u,0xffffffffu))==0xff00ff00u;"
    "const c1 = pack4xI8(vec4i(-129,-128,127,128))==0x807f807fu;"
    "const c2 = pack4xU8Clamp(vec4u(0u,255u,256u,0xffffffffu))==0xffffff00u;"
    "const c3 = pack4xI8Clamp(vec4i(-129,-128,127,128))==0x7f7f8080u;"
    "const c4 = all(unpack4xU8(0x807fff01u)==vec4u(1u,255u,127u,128u));"
    "const c5 = all(unpack4xI8(0x807fff01u)==vec4i(1,-1,127,-128));"
    "const c6 = dot4I8Packed(0x807fff01u,0x02ff0380u)==-514i;"
    "const c7 = dot4U8Packed(0x807fff01u,0x02ff0380u)==33534u;"
    "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
    "fn left()->u32{data[14]=data[14]*10u+1u;return 0x807fff01u;}"
    "fn right()->u32{data[14]=data[14]*10u+2u;return 0x02ff0380u;}"
    "@compute @workgroup_size(1) fn main(){"
    "var u=vec4u(0u,255u,256u,0xffffffffu);var s=vec4i(-129,-128,127,128);"
    "data[0]=pack4xU8(u);data[1]=pack4xI8(s);"
    "data[2]=pack4xU8Clamp(u);data[3]=pack4xI8Clamp(s);"
    "var p=0x807fff01u;var q=0x02ff0380u;"
    "let a=unpack4xU8(p);let b=unpack4xI8(p);"
    "data[4]=a.x;data[5]=a.y;data[6]=a.z;data[7]=a.w;"
    "data[8]=bitcast<u32>(b.x);data[9]=bitcast<u32>(b.y);"
    "data[10]=bitcast<u32>(b.z);data[11]=bitcast<u32>(b.w);"
    "data[12]=bitcast<u32>(dot4I8Packed(p,q));data[13]=dot4U8Packed(p,q);"
    "data[14]=0u;data[15]=dot4U8Packed(left(),right());data[16]=u32(c0);data[17]=u32(c1);data[18]=u32(c2);data[19]=u32(c3);data[20]=u32(c4);data[21]=u32(c5);data[22]=u32(c6);data[23]=u32(c7);}"
;

#endif
