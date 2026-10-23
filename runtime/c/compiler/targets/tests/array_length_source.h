/* SPDX-License-Identifier: MIT
 * One source fixture for both text targets, not a second semantic evaluator. */
#ifndef DOE_TARGET_ARRAY_LENGTH_SOURCE_H
#define DOE_TARGET_ARRAY_LENGTH_SOURCE_H
static const char array_length_source[] =
    "struct Tail{prefix:vec4u,values:array<vec4u>}"
    "@group(0) @binding(0) var<storage,read> input:Tail;"
    "@group(0) @binding(1) var<storage,read_write> output:array<u32>;"
    "fn length()->u32{return arrayLength(&(input.values));}"
    "@compute @workgroup_size(1) fn array_length(){"
    "output[0]=length();output[1]=arrayLength(&output);}";
#endif
