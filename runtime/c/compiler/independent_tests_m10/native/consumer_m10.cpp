/* SPDX-License-Identifier: MIT
 * Separately compiled C++ consumer, public headers only, no RTTI/exceptions. */
#include "doe_compiler_typed.h"
#include "doe_compiler_job.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
static_assert(std::is_standard_layout<DoeCompilerTypedCapacities>::value,"C capacities");
static_assert(std::is_standard_layout<DoeCompilerTypedResult>::value,"C result");
static_assert(sizeof(DoeCompilerNameV4)==8u&&sizeof(DoeCompilerTypeV4)==64u&&sizeof(DoeCompilerMemberV4)==56u&&sizeof(DoeCompilerResourceV4)==80u&&sizeof(DoeCompilerModuleV4)==32u,"fixed record sizes");
static_assert(offsetof(DoeCompilerTypedCapacities,expression_depth)==15u*sizeof(std::size_t),"capacity count fields");
static_assert(offsetof(DoeCompilerTypeV4,size_bytes)==40u&&offsetof(DoeCompilerMemberV4,offset_bytes)==16u&&offsetof(DoeCompilerResourceV4,minimum_binding_size_bytes)==32u,"fixed field offsets");
static_assert(DOE_COMPILER_OK==0&&DOE_COMPILER_INPUT_ERROR==1&&DOE_COMPILER_PARSE_ERROR==2&&DOE_COMPILER_UNSUPPORTED==3&&DOE_COMPILER_SEMANTIC_ERROR==4&&DOE_COMPILER_OUTPUT_TOO_SMALL==5&&DOE_COMPILER_ABI_MISMATCH==6,"unchanged status values");
static const char source[]="struct Unused { a:vec2<f32>, } @group(0) @binding(0) var<storage,read_write> data:array<vec2<u32>>; @compute @workgroup_size(1) fn cpp_entry(){data[0u]=vec2<u32>(3u,5u);}";
int main(){const DoeCompilerTypedCapacities c{65536u,8192u,1024u,4096u,256u,1024u,32u,5121u,1024u,4096u,24577u,4096u,7744u,1281u,65536u,32u,16u,32u,0u};DoeCompilerWorkspaceRequirements q{};if(doe_compiler_typed_requirements(4u,&c,sizeof(c),&q,sizeof(q))!=DOE_COMPILER_OK||q.alignment_bytes>alignof(std::max_align_t))return 1;void *work=std::malloc(q.size_bytes);if(!work)return 2;DoeCompilerTypedResult m{};
 if(doe_compiler_compile_typed(4u,&c,sizeof(c),work,q.size_bytes,source,sizeof(source)-1u,7u,nullptr,0u,&m,sizeof(m))!=DOE_COMPILER_OK||m.required_is_exact!=1u)return 3;
 DoeCompilerTypedOutputs o{};o.word_capacity=m.required.words;o.type_capacity=m.required.types;o.member_capacity=m.required.members;o.resource_capacity=m.required.resources;o.name_capacity=m.required.name_bytes;o.type_record_size=sizeof(DoeCompilerTypeV4);o.member_record_size=sizeof(DoeCompilerMemberV4);o.resource_record_size=sizeof(DoeCompilerResourceV4);
 o.words=static_cast<std::uint32_t *>(std::malloc(o.word_capacity*sizeof(*o.words)));o.types=static_cast<DoeCompilerTypeV4 *>(std::malloc(o.type_capacity*sizeof(*o.types)));o.members=static_cast<DoeCompilerMemberV4 *>(std::malloc(o.member_capacity*sizeof(*o.members)));o.resources=static_cast<DoeCompilerResourceV4 *>(std::malloc(o.resource_capacity*sizeof(*o.resources)));o.names=static_cast<char *>(std::malloc(o.name_capacity));if(!o.words||!o.types||!o.members||!o.resources||!o.names)return 4;DoeCompilerTypedResult r{};
 auto status=doe_compiler_compile_typed(4u,&c,sizeof(c),work,q.size_bytes,source,sizeof(source)-1u,3u,&o,sizeof(o),&r,sizeof(r));std::memset(work,0xab,q.size_bytes);std::free(work);
 if(status!=DOE_COMPILER_OK||r.required_is_exact!=1u||r.published.words!=m.required.words||r.published.types!=m.required.types||r.published.members!=m.required.members||r.published.resources!=m.required.resources||r.published.name_bytes!=m.required.name_bytes)return 5;
 if(r.module.declared_struct_count!=1u||r.module.entry_point.length!=9u||std::strcmp(o.names+r.module.entry_point.offset,"cpp_entry")!=0||std::strcmp(o.names+o.types[0].name.offset,"Unused")!=0)return 6;
 std::free(o.words);std::free(o.types);std::free(o.members);std::free(o.resources);std::free(o.names);std::puts("PASS separately compiled C++17 native ABI4 consumer");return 0;}
