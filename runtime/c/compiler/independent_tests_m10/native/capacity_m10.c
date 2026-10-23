/* SPDX-License-Identifier: MIT
 * Bounded pool discovery tests. No guessed private sizes or target-ID maps. */
#include "support_m10.h"
static const char pool_source[]=
 "struct Inner { a: vec3<u32>, b: u32, }\n"
 "struct Outer { left: Inner, right: array<vec2<u32>, 3>, }\n"
 "const p = Outer(Inner(vec3<u32>(2u,3u,5u),7u),array<vec2<u32>,3>(vec2<u32>(11u,13u),vec2<u32>(17u,19u),vec2<u32>(23u,29u)));\n"
 "const picked=p.right[1u].x;\n"
 "@group(0) @binding(0) var<storage,read_write> data:array<Outer>;\n"
 "fn choose(x:u32)->vec2<u32>{return vec2<u32>(x,picked);}\n"
 "@compute @workgroup_size(1) fn pool(@builtin(global_invocation_id) id:vec3<u32>){var q:Outer=p;q.right[2u]=choose(id.x);if(id.x==0u){data[id.x]=q;}}\n";
typedef struct { const char *name;size_t offset;unsigned pool; } Pool;
static const Pool pools[]={
 {"source",offsetof(DoeCompilerTypedCapacities,source_bytes),1u},{"tokens",offsetof(DoeCompilerTypedCapacities,tokens),2u},
 {"statements",offsetof(DoeCompilerTypedCapacities,statements),3u},{"expressions",offsetof(DoeCompilerTypedCapacities,expressions),4u},
 {"types",offsetof(DoeCompilerTypedCapacities,types),5u},{"members",offsetof(DoeCompilerTypedCapacities,members),6u},
 {"resources",offsetof(DoeCompilerTypedCapacities,resources),7u},{"access_steps",offsetof(DoeCompilerTypedCapacities,access_steps),8u},
 {"constants",offsetof(DoeCompilerTypedCapacities,constants),9u},{"constant_edges",offsetof(DoeCompilerTypedCapacities,constant_edges),10u},
 {"ir",offsetof(DoeCompilerTypedCapacities,ir_instructions),11u},{"operands",offsetof(DoeCompilerTypedCapacities,ir_operands),12u},
 {"target_declarations",offsetof(DoeCompilerTypedCapacities,target_declarations),13u},{"traversal",offsetof(DoeCompilerTypedCapacities,traversal_items),15u},
 {"words",offsetof(DoeCompilerTypedCapacities,words),16u}
};
static size_t *slot(DoeCompilerTypedCapacities *c,const Pool *p){return (size_t *)((unsigned char *)c+p->offset);}
static int attempt(const Pool *p,size_t limit,unsigned flags,Bulk *b,DoeCompilerStatus *returned){
 DoeCompilerTypedCapacities caps=generous;*slot(&caps,p)=limit;Client c;CHECK(client_new(&c,&caps)==0,"pool query and allocation");DoeCompilerTypedOutputs o=selected(b,flags);DoeCompilerTypedResult r;
 for(unsigned repeat=0;repeat<2u;++repeat){bulk_mark(b);DoeCompilerStatus s=invoke(&c,pool_source,sizeof(pool_source)-1u,flags,(flags&4u)?NULL:&o,&r);
  if(repeat)CHECK(s==*returned,"reuse same constrained workspace without reset");else *returned=s;
  if(s!=DOE_COMPILER_OK){CHECK(s==(p->pool==16u?DOE_COMPILER_OUTPUT_TOO_SMALL:DOE_COMPILER_INPUT_ERROR),"pool status");CHECK(failure_result(&r,s,0)==0,"scratch lower bound never exact counts");
   CHECK(r.exhausted_pool==p->pool,"isolated named pool reached");CHECK(r.minimum_pool_capacity>limit,"attempted extent exceeds configured capacity");CHECK(bulk_unchanged(b),"scratch failure all outputs unchanged");
  }else CHECK(r.required_is_exact==1u,"successful pool exact counts");
  CHECK(guard_ok(&c.workspace)&&bulk_guards(b),"pool redzones");
 }
 printf("pool=%s limit=%zu flags=%u status=%d exhausted=%u lower_bound=%zu\n",p->name,limit,flags,(int)*returned,r.exhausted_pool,r.minimum_pool_capacity);client_free(&c);return 0;
}
static int depths(Bulk *b){
 static const char expressions[]="@compute @workgroup_size(1) fn d(){let a=((((((((1u))))))));}";
 static const char controls[]="@compute @workgroup_size(1) fn d(){if(true){if(true){if(true){if(true){}}}}}";
 static const char types[]="struct D {a:array<array<array<array<u32,2>,2>,2>,2>,} @compute @workgroup_size(1) fn d(){}";
 const char *source[]={expressions,controls,types};
 for(unsigned k=0;k<3u;++k){DoeCompilerTypedCapacities caps=generous;if(k==0u)caps.expression_depth=1u;if(k==1u)caps.control_depth=1u;if(k==2u)caps.type_syntax_depth=1u;Client c;CHECK(client_new(&c,&caps)==0,"depth query");
  for(unsigned f=1u;f<=7u;++f){if(f==4u)continue;DoeCompilerTypedOutputs o=selected(b,f);DoeCompilerTypedResult r;bulk_mark(b);CHECK(invoke(&c,source[k],strlen(source[k]),f,(f&4u)?NULL:&o,&r)==DOE_COMPILER_INPUT_ERROR,"depth failure category");CHECK(r.exhausted_pool==17u+k&&r.minimum_pool_capacity>1u,"named depth bound");CHECK(failure_result(&r,DOE_COMPILER_INPUT_ERROR,0)==0&&bulk_unchanged(b),"depth no publication");}
  DoeCompilerTypedResult r;CHECK(invoke(&c,"@compute @workgroup_size(1) fn shallow(){}",strlen("@compute @workgroup_size(1) fn shallow(){}"),5u,NULL,&r)==DOE_COMPILER_OK,"reuse depth workspace with shallow program");client_free(&c);
 }return 0;
}
int main(void){CHECK(native_allocator_control()==0,"allocator interception positive control");Client base;CHECK(client_new(&base,&generous)==0,"capacity baseline");DoeCompilerTypedResult measured;CHECK(invoke(&base,pool_source,sizeof(pool_source)-1u,7u,NULL,&measured)==DOE_COMPILER_OK,"known admitted pool fixture");Bulk b;CHECK(bulk_new(&b,&measured.required)==0,"pool outputs");client_free(&base);
 for(unsigned i=0;i<COUNT(pools);++i){const Pool *p=&pools[i];DoeCompilerTypedCapacities caps=generous;size_t lo=0u,hi=*slot(&caps,p);DoeCompilerStatus status;
  CHECK(attempt(p,0u,3u,&b,&status)==0&&status!=DOE_COMPILER_OK,"zero pool must be exercised");
  while(lo+1u<hi){size_t mid=lo+(hi-lo)/2u;CHECK(attempt(p,mid,3u,&b,&status)==0,"bounded boundary discovery");if(status==DOE_COMPILER_OK)hi=mid;else lo=mid;}
  for(unsigned f=1u;f<=7u;++f){if(f==4u)continue;CHECK(attempt(p,hi,f,&b,&status)==0&&status==DOE_COMPILER_OK,"exact sufficient pool every mode");CHECK(attempt(p,hi-1u,f,&b,&status)==0&&status!=DOE_COMPILER_OK,"one-short pool every mode");}
 }
 CHECK(depths(&b)==0,"explicit depth limits");bulk_free(&b);CHECK(atomic_load(&native_allocations)==0u,"no allocation in query/compile");puts("PASS named explicit pools at zero/exact/short, same limits in every output mode; TYPE_SYNTAX is token-derived and not independently configurable");return 0;}
