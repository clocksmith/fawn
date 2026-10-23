/* SPDX-License-Identifier: MIT */
#ifndef DOE_TARGET_MATRIX_SOURCE_H
#define DOE_TARGET_MATRIX_SOURCE_H
static const char matrix_control_source[] =
    "struct Data{m:mat3x2f, guard:u32}struct Padded{m:mat2x3f,guard:u32}"
    "@group(0) @binding(0) var<storage,read_write> output:array<f32>;"
    "@group(0) @binding(1) var<uniform> input:Data;"
    "@group(0) @binding(2) var<storage,read_write> matrices:array<mat3x2f>;"
    "@group(0) @binding(3) var<uniform> padded:Padded;"
    "@group(0) @binding(4) var<storage,read_write> padded_output:array<mat2x3f>;"
    "@compute @workgroup_size(1) fn matrix_control(){"
    "let a=input.m; let v=vec3f(1.0,2.0,3.0); let b=a*v;"
    "output[0]=b.x; output[1]=b.y; let c=vec2f(2.0,3.0)*a;"
    "output[2]=c.x; output[3]=c.y; output[4]=c.z;"
    "let product=a*transpose(a); output[5]=product[0][0];"
    "output[6]=product[0][1]; output[7]=product[1][1];"
    "let square=mat2x2f(1.0,2.0,3.0,5.0); output[8]=determinant(square);"
    "let doubled=a*2.0; let added=doubled-a; matrices[0]=added;"
    "let missing=matrices[2]; output[9]=missing[0][0];"
    "var nested:array<mat3x2f,2>; output[10]=nested[1][2][1];"
    "output[11]=ceil(a[0][0]);output[12]=padded.m[0][2];"
    "output[13]=padded.m[1][2];padded_output[0]=padded.m;"
    "let scalar_constructor=mat2x2f(a[0][0],a[0][1],a[1][0],a[1][1]);"
    "let column_constructor=mat3x2f(a[0],a[1],a[2]);"
    "output[14]=scalar_constructor[1][1];output[15]=column_constructor[2][1];"
    "let ordered_product=square*scalar_constructor;output[16]=ordered_product[1][0];}";
#endif
