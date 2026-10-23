/* SPDX-License-Identifier: MIT
 * Independent M10 native evaluator. Frozen public ABI only. */
#ifndef NATIVE_SUPPORT_M11_H
#define NATIVE_SUPPORT_M11_H
#include "doe_compiler_typed.h"
#include "doe_compiler_job.h"
#include <errno.h>
#include <fenv.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#define MARK 0xa7u
#define CHECK(c,m) do { if (!(c)) { fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m); return 1; } } while (0)
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
extern _Thread_local unsigned native_inside;
extern atomic_uint native_allocations;
int native_allocator_control(void);
static const char aggregate_source[] =
 "struct Pebble { shade: vec3<f32>, weight: u32, }\n"
 "struct Bundle { left: Pebble, right: array<vec2<u32>, 3>, }\n"
 "struct Spare { hidden: array<vec4<i32>, 2>, }\n"
 "@group(0) @binding(0) var<storage,read_write> items: array<Bundle>;\n"
 "fn make(x:u32)->vec2<u32>{return vec2<u32>(x,x+1u);}\n"
 "@compute @workgroup_size(2,3,1) fn dispatch(@builtin(global_invocation_id) p:vec3<u32>){\n"
 "var b: Bundle; b.right[1u]=make(p.x); items[p.x]=b; }\n";
static const char numeric_source[] =
 "struct Decimal { value: vec2<f32>, }\n"
 "var<workgroup> shared_value: vec2<f32>;\n"
 "@group(0) @binding(0) var<storage,read_write> output: array<f32>;\n"
 "const k = vec2<f32>(0.1, 0.2) + vec2<f32>(0.2, 0.3);\n"
 "fn synchronize(){workgroupBarrier();}\n"
 "@compute @workgroup_size(2) fn decimal(@builtin(local_invocation_index) i:u32){\n"
 "if(i==0u){shared_value=k;} synchronize(); output[i]=shared_value.x;}\n";
static const char old_source[] =
 "@group(0) @binding(0) var<storage,read_write> output: array<u32>;\n"
 "@compute @workgroup_size(1) fn old_entry(){output[0u]=7u;}\n";
static const char local_source[] =
 "@group(0) @binding(0) var<storage,read_write> output: array<u32>;\n"
 "@compute @workgroup_size(1) fn local_entry(){var a:array<u32,2>;a[1u]=3u;output[0u]=a[1u];}\n";
static const char parse_source[] = "@compute @workgroup_size(1) fn broken( {";
static const char semantic_source[] =
 "var<workgroup> shared_value:u32;\n"
 "@compute @workgroup_size(2) fn broken(@builtin(local_invocation_index) i:u32){\n"
 "if(i==0u){workgroupBarrier();}}\n";
static const char unsupported_source[] = "@compute @workgroup_size(1) fn broken(){var a:mat2x2<f32>;}";
static const DoeCompilerTypedCapacities generous = {
 65536u,8192u,1024u,4096u,256u,1024u,32u,5121u,
 1024u,4096u,24577u,4096u,7744u,1281u,65536u,32u,16u,32u,0u
};
static inline int bytes_are(const void *p,size_t n,unsigned char v) {
 const unsigned char *b=(const unsigned char *)p;
 for(size_t i=0;i<n;++i) if(b[i]!=v) return 0;
 return 1;
}
typedef struct { unsigned char *allocation,*data;size_t bytes,total,prefix; } Guard;
static inline int guard_new(Guard *g,size_t bytes) {
 const size_t a=_Alignof(max_align_t),prefix=((64u+a-1u)/a)*a;
 memset(g,0,sizeof(*g)); if(bytes>SIZE_MAX-prefix-64u) return 1;
 g->allocation=(unsigned char *)malloc(prefix+bytes+64u); if(!g->allocation) return 1;
 g->data=g->allocation+prefix;g->bytes=bytes;g->prefix=prefix;g->total=prefix+bytes+64u;
 memset(g->allocation,MARK,g->total);return 0;
}
static inline void guard_free(Guard *g) {free(g->allocation);memset(g,0,sizeof(*g));}
static inline int guard_ok(const Guard *g) {
 return bytes_are(g->allocation,g->prefix,MARK)&&bytes_are(g->data+g->bytes,64u,MARK);
}
static inline void guard_mark(Guard *g) {memset(g->data,MARK,g->bytes);}
typedef struct {DoeCompilerTypedCapacities caps;DoeCompilerWorkspaceRequirements req;Guard workspace;} Client;
static inline DoeCompilerStatus query(const DoeCompilerTypedCapacities *c,DoeCompilerWorkspaceRequirements *r) {
 ++native_inside;DoeCompilerStatus s=doe_compiler_typed_requirements(4u,c,sizeof(*c),r,sizeof(*r));--native_inside;return s;
}
static inline int client_new(Client *c,const DoeCompilerTypedCapacities *caps) {
 memset(c,0,sizeof(*c));c->caps=*caps;
 CHECK(query(&c->caps,&c->req)==DOE_COMPILER_OK,"query selected capacities");
 CHECK(c->req.size_bytes>0u && c->req.alignment_bytes>0u,"nonzero requirements");
 CHECK(c->req.alignment_bytes<=_Alignof(max_align_t),"malloc-compatible alignment");
 CHECK((_Alignof(max_align_t)%c->req.alignment_bytes)==0u,"valid fundamental alignment");
 CHECK(guard_new(&c->workspace,c->req.size_bytes)==0,"caller allocation");return 0;
}
static inline void client_free(Client *c) {guard_free(&c->workspace);}
typedef struct { Guard span[5];DoeCompilerTypedOutputs o; } Bulk;
static inline int bulk_new(Bulk *b,const DoeCompilerTypedOutputCounts *n) {
 const size_t counts[5]={n->words,n->types,n->members,n->resources,n->name_bytes};
 const size_t sizes[5]={sizeof(uint32_t),sizeof(DoeCompilerTypeV4),sizeof(DoeCompilerMemberV4),sizeof(DoeCompilerResourceV4),1u};
 memset(b,0,sizeof(*b));
 for(unsigned i=0;i<5u;++i) {CHECK(counts[i]<=SIZE_MAX/sizes[i],"harness extent product");CHECK(guard_new(&b->span[i],counts[i]*sizes[i])==0,"caller output allocation");}
 b->o.words=(uint32_t *)b->span[0].data;b->o.word_capacity=n->words;
 b->o.types=(DoeCompilerTypeV4 *)b->span[1].data;b->o.type_capacity=n->types;b->o.type_record_size=sizeof(DoeCompilerTypeV4);
 b->o.members=(DoeCompilerMemberV4 *)b->span[2].data;b->o.member_capacity=n->members;b->o.member_record_size=sizeof(DoeCompilerMemberV4);
 b->o.resources=(DoeCompilerResourceV4 *)b->span[3].data;b->o.resource_capacity=n->resources;b->o.resource_record_size=sizeof(DoeCompilerResourceV4);
 b->o.names=(char *)b->span[4].data;b->o.name_capacity=n->name_bytes;return 0;
}
static inline void bulk_free(Bulk *b){for(unsigned i=0;i<5u;++i)guard_free(&b->span[i]);}
static inline void bulk_mark(Bulk *b){for(unsigned i=0;i<5u;++i)guard_mark(&b->span[i]);}
static inline int bulk_guards(const Bulk *b){for(unsigned i=0;i<5u;++i)if(!guard_ok(&b->span[i]))return 0;return 1;}
static inline int bulk_unchanged(const Bulk *b){for(unsigned i=0;i<5u;++i)if(!bytes_are(b->span[i].data,b->span[i].bytes,MARK))return 0;return bulk_guards(b);}
static inline DoeCompilerTypedOutputs selected(Bulk *b,unsigned flags) {
 DoeCompilerTypedOutputs o=b->o;
 if(!(flags&1u)){o.words=NULL;o.word_capacity=0u;}
 if(!(flags&2u)){o.types=NULL;o.type_capacity=0u;o.members=NULL;o.member_capacity=0u;o.resources=NULL;o.resource_capacity=0u;o.names=NULL;o.name_capacity=0u;}
 return o;
}
static inline DoeCompilerStatus invoke(Client *c,const char *s,size_t n,unsigned flags,const DoeCompilerTypedOutputs *o,DoeCompilerTypedResult *r) {
 ++native_inside;DoeCompilerStatus st=doe_compiler_compile_typed(4u,&c->caps,sizeof(c->caps),c->workspace.data,c->req.size_bytes,s,n,flags,o,o?sizeof(*o):0u,r,sizeof(*r));--native_inside;return st;
}
static inline int counts_equal(const DoeCompilerTypedOutputCounts *a,const DoeCompilerTypedOutputCounts *b) {
 return a->words==b->words&&a->types==b->types&&a->members==b->members&&a->resources==b->resources&&a->name_bytes==b->name_bytes;
}
static inline int failure_result(const DoeCompilerTypedResult *r,DoeCompilerStatus st,int exact) {
 CHECK(r->diagnostic.status==st && r->diagnostic.message[0]!=0,"failure diagnostic");
 CHECK(r->required_is_exact==(unsigned)exact,"failure exactness");
 CHECK(bytes_are(&r->published,sizeof(r->published),0)&&bytes_are(&r->module,sizeof(r->module),0),"failure publishes no control fields");
 if(!exact)CHECK(bytes_are(&r->required,sizeof(r->required),0),"inexact failure has no required counts");return 0;
}
#endif
