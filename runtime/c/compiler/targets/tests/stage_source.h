/* SPDX-License-Identifier: MIT
 * Shared source witnesses; target expectations and emitted-operation checks stay separate. */
#ifndef DOE_TARGET_STAGE_SOURCE_H
#define DOE_TARGET_STAGE_SOURCE_H
static const char target_graphics_mapping[] =
        "struct Output{@builtin(position) p:vec4f,"
        "@location(3) @interpolate(flat) index:u32,"
        "@location(0) @interpolate(linear,centroid) color:vec4f}"
        "@vertex fn vertex(@builtin(vertex_index) v:u32,@builtin(instance_index) i:u32,"
        "@location(2) p:vec4f)->Output{return Output(p,v+i,vec4f(0.5));}"
        "@fragment fn fragment(input:Output)->@location(0) vec4f{return input.color;}"
        "@compute @workgroup_size(1) fn compute(){}";
static const char target_fragment_builtins[] =
        "struct Input{@builtin(front_facing) face:bool,@builtin(sample_index) index:u32,"
        "@builtin(sample_mask) mask:u32,@builtin(position) p:vec4f,"
        "@location(0) @interpolate(perspective,sample) color:vec4f}"
        "struct Output{@builtin(frag_depth) depth:f32,@builtin(sample_mask) mask:u32,"
        "@location(0) color:vec4f}"
        "@fragment fn fragment(input:Input)->Output{"
        "if(input.face){return Output(input.p.z,input.mask,input.color);}"
        "return Output(0.5,input.index,vec4f(0.0));}";
static const char target_empty_fragment[] ="@fragment fn empty(){}";
static const char target_invariant_vertex[] =
        "@vertex fn vertex()->@builtin(position) @invariant vec4f{return vec4f(0.0);}";
static const char target_graphics_control[] =
        "struct Input{@location(2) @interpolate(flat) left:u32,"
        "@location(0) @interpolate(flat) right:u32,@builtin(front_facing) face:bool}"
        "struct Output{@location(1) value:u32,@builtin(sample_mask) mask:u32}"
        "fn combine(left:u32,right:u32)->u32{return left*100u+right;}"
        "@fragment fn graphics(input:Input,@builtin(sample_index) sample:u32)->Output{"
        "let value=combine(input.left,input.right);"
        "if(input.face){return Output(value,sample);}return Output(value+1u,0u);}";
#endif
