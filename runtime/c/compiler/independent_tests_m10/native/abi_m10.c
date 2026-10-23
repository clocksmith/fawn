/* SPDX-License-Identifier: MIT */
#include "support_m10.h"
_Static_assert(sizeof(DoeCompilerNameV4)==8u,"fixed name record");
_Static_assert(sizeof(DoeCompilerTypeV4)==64u,"fixed type record");
_Static_assert(sizeof(DoeCompilerMemberV4)==56u,"fixed member record");
_Static_assert(sizeof(DoeCompilerResourceV4)==80u,"fixed resource record");
_Static_assert(sizeof(DoeCompilerModuleV4)==32u,"fixed module record");
_Static_assert(DOE_COMPILER_TYPED_ABI_VERSION==4u && DOE_COMPILER_TYPED_SCHEMA_VERSION==4u,"ABI/schema");
static int success_controls(const DoeCompilerTypedResult *r,unsigned flags,const DoeCompilerTypedOutputCounts *all) {
 DoeCompilerTypedOutputCounts n=*all;if(!(flags&1u))n.words=0u;if(!(flags&2u))n.types=n.members=n.resources=n.name_bytes=0u;
 CHECK(r->required_is_exact==1u&&counts_equal(&r->required,&n),"selected exact counts");
 CHECK(r->exhausted_pool==0u&&r->minimum_pool_capacity==0u,"success no exhaustion");
 CHECK(r->diagnostic.status==DOE_COMPILER_OK,"success diagnostic status");
 if(flags&4u)CHECK(bytes_are(&r->published,sizeof(r->published),0),"measure no publication");
 else CHECK(counts_equal(&r->published,&n),"normal complete publication");
 if((flags&4u)||!(flags&2u))CHECK(bytes_are(&r->module,sizeof(r->module),0),"unselected module zero");
 else {CHECK(r->module.schema_version==4u&&r->module.layout_profile==1u,"module versions");CHECK(r->module.declared_struct_count==3u,"all unused structures declared");CHECK(r->module.workgroup_size[0]==2u&&r->module.workgroup_size[1]==3u&&r->module.workgroup_size[2]==1u,"workgroup output");}return 0;
}
static int consume_name(const Bulk *b,DoeCompilerNameV4 n,size_t *cursor,const char *expected) {
 size_t length=strlen(expected);CHECK(n.offset==*cursor&&n.length==length,"owned name order and length");
 CHECK(*cursor+length+1u<=b->o.name_capacity,"name range");CHECK(memcmp(b->o.names+*cursor,expected,length+1u)==0,"name bytes and one NUL");*cursor+=length+1u;return 0;
}
static int reflection_shape(const Bulk *b,const DoeCompilerTypedResult *r) {
 size_t cursor=0;CHECK(consume_name(b,r->module.entry_point,&cursor,"dispatch")==0,"entry name");
 const char *names[]={"Pebble","Bundle","Spare"};
 for(size_t i=0;i<r->published.types;++i){
  const DoeCompilerTypeV4 *t=&b->o.types[i];DoeCompilerTypeV4 z;memset(&z,0,sizeof(z));
  z.kind=t->kind;z.flags=t->flags;z.name=t->name;z.scalar_kind=t->scalar_kind;z.element_type=t->element_type;z.vector_width=t->vector_width;z.array_count=t->array_count;z.first_member=t->first_member;z.member_count=t->member_count;z.size_bytes=t->size_bytes;z.alignment_bytes=t->alignment_bytes;z.stride_bytes=t->stride_bytes;
  CHECK(memcmp(t,&z,sizeof(z))==0,"type padding initialized");CHECK(t->kind>=1u&&t->kind<=5u&&(t->flags&~15u)==0u,"type enum/flags");
  if(i<3u){CHECK(t->kind==4u,"nominal types first");CHECK(consume_name(b,t->name,&cursor,names[i])==0,"type names");}
  else CHECK(t->name.offset==0u&&t->name.length==0u,"anonymous names empty");
  if(t->kind!=1u)CHECK(t->scalar_kind==0u,"irrelevant leaf zero");else CHECK(t->scalar_kind>=1u&&t->scalar_kind<=4u,"leaf enum");
  if(t->kind==2u||t->kind==3u||t->kind==5u)CHECK(t->element_type<r->published.types,"element index range");else CHECK(t->element_type==UINT32_MAX,"no element sentinel");
  if(t->kind!=2u)CHECK(t->vector_width==0u,"irrelevant vector width");else CHECK(t->vector_width>=2u&&t->vector_width<=4u,"vector width");
  if(t->kind!=3u)CHECK(t->array_count==0u,"irrelevant array count");else CHECK(t->array_count>0u,"positive array count");
  if(t->kind!=4u)CHECK(t->first_member==0u&&t->member_count==0u,"nonstruct member fields zero");
  else CHECK(t->first_member<=r->published.members&&t->member_count<=r->published.members-t->first_member,"member range");
  if(!(t->flags&4u))CHECK(t->size_bytes==0u,"unknown size zero");
  if(t->kind!=3u&&t->kind!=5u)CHECK(t->stride_bytes==0u,"irrelevant stride");
 }
 const char *members[]={"shade","weight","left","right","hidden"};CHECK(r->published.members==COUNT(members),"all member declarations");
 for(size_t i=0;i<r->published.members;++i){
  const DoeCompilerMemberV4 *m=&b->o.members[i];DoeCompilerMemberV4 z;memset(&z,0,sizeof(z));
  z.name=m->name;z.type_index=m->type_index;z.flags=m->flags;z.offset_bytes=m->offset_bytes;z.size_bytes=m->size_bytes;z.alignment_bytes=m->alignment_bytes;z.explicit_align_bytes=m->explicit_align_bytes;z.explicit_size_bytes=m->explicit_size_bytes;
  CHECK(memcmp(m,&z,sizeof(z))==0,"member padding initialized");CHECK(consume_name(b,m->name,&cursor,members[i])==0,"member names");CHECK(m->type_index<r->published.types,"member type range");CHECK(m->flags==0u&&m->explicit_align_bytes==0u&&m->explicit_size_bytes==0u,"absent attributes zero");
 }
 CHECK(r->published.resources==1u,"resource declarations");
 const DoeCompilerResourceV4 *p=b->o.resources;DoeCompilerResourceV4 z;memset(&z,0,sizeof(z));
 z.name=p->name;z.group=p->group;z.binding=p->binding;z.kind=p->kind;z.access=p->access;z.type_index=p->type_index;z.flags=p->flags;z.minimum_binding_size_bytes=p->minimum_binding_size_bytes;z.type_alignment_bytes=p->type_alignment_bytes;z.address_space_alignment_bytes=p->address_space_alignment_bytes;z.runtime_array_offset_bytes=p->runtime_array_offset_bytes;z.runtime_array_stride_bytes=p->runtime_array_stride_bytes;z.runtime_array_type=p->runtime_array_type;z.reserved=p->reserved;
 CHECK(memcmp(p,&z,sizeof(z))==0&&p->reserved==0u,"resource padding and reserved zero");CHECK(consume_name(b,p->name,&cursor,"items")==0,"resource name");CHECK(p->type_index<r->published.types&&p->runtime_array_type<r->published.types,"resource indices");CHECK(cursor==r->published.name_bytes,"exact name byte count");return 0;
}
static int tail_unchanged(const Bulk *b,const DoeCompilerTypedOutputCounts *n) {
 const size_t used[5]={n->words*sizeof(uint32_t),n->types*sizeof(DoeCompilerTypeV4),n->members*sizeof(DoeCompilerMemberV4),n->resources*sizeof(DoeCompilerResourceV4),n->name_bytes};
 for(unsigned i=0;i<5u;++i){CHECK(used[i]<=b->span[i].bytes,"published prefix bounded");CHECK(bytes_are(b->span[i].data+used[i],b->span[i].bytes-used[i],MARK),"unpublished full-capacity suffix untouched");}CHECK(bulk_guards(b),"all output redzones");return 0;
}
static int mode_matrix(Client *c,DoeCompilerTypedOutputCounts *all) {
 DoeCompilerTypedResult measure;CHECK(invoke(c,aggregate_source,sizeof(aggregate_source)-1u,7u,NULL,&measure)==DOE_COMPILER_OK,"measure complete module");*all=measure.required;
 CHECK(all->words>5u&&all->types>3u&&all->members==5u&&all->resources==1u&&all->name_bytes>0u,"positive discovery counts");
 DoeCompilerTypedOutputCounts extra=*all;extra.words+=3u;extra.types+=3u;extra.members+=3u;extra.resources+=3u;extra.name_bytes+=3u;Bulk b;CHECK(bulk_new(&b,&extra)==0,"output allocation");
 const unsigned flags[]={1u,2u,3u,5u,6u,7u};
 for(unsigned i=0;i<COUNT(flags);++i){unsigned f=flags[i];DoeCompilerTypedResult r;bulk_mark(&b);DoeCompilerTypedOutputs o=selected(&b,f);
  CHECK(invoke(c,aggregate_source,sizeof(aggregate_source)-1u,f,(f&4u)?NULL:&o,&r)==DOE_COMPILER_OK,"flag truth table admitted");CHECK(success_controls(&r,f,all)==0,"flag controls");CHECK(tail_unchanged(&b,&r.published)==0,"mode suffix");if(f==3u)CHECK(reflection_shape(&b,&r)==0,"reflection shape");
 }
 for(unsigned flagsel=1u;flagsel<=3u;++flagsel)for(unsigned short_span=0u;short_span<5u;++short_span){if((short_span==0u&&!(flagsel&1u))||(short_span!=0u&&!(flagsel&2u)))continue;
  DoeCompilerTypedOutputs o=selected(&b,flagsel);o.word_capacity=(flagsel&1u)?all->words:0u;o.type_capacity=(flagsel&2u)?all->types:0u;o.member_capacity=(flagsel&2u)?all->members:0u;o.resource_capacity=(flagsel&2u)?all->resources:0u;o.name_capacity=(flagsel&2u)?all->name_bytes:0u;
  size_t *caps[]={&o.word_capacity,&o.type_capacity,&o.member_capacity,&o.resource_capacity,&o.name_capacity};--*caps[short_span];bulk_mark(&b);DoeCompilerTypedResult r;
  CHECK(invoke(c,aggregate_source,sizeof(aggregate_source)-1u,flagsel,&o,&r)==DOE_COMPILER_OUTPUT_TOO_SMALL,"one selected short span");CHECK(failure_result(&r,DOE_COMPILER_OUTPUT_TOO_SMALL,1)==0,"exact shortage failure");
  DoeCompilerTypedOutputCounts expect=*all;if(!(flagsel&1u))expect.words=0u;if(!(flagsel&2u))expect.types=expect.members=expect.resources=expect.name_bytes=0u;
  CHECK(counts_equal(&r.required,&expect)&&bulk_unchanged(&b),"all counts known and every span atomic");
  o=selected(&b,flagsel);CHECK(invoke(c,aggregate_source,sizeof(aggregate_source)-1u,flagsel,&o,&r)==DOE_COMPILER_OK,"reuse without reset after output shortage");
 }
 DoeCompilerTypedOutputs zero;memset(&zero,0,sizeof(zero));zero.type_record_size=sizeof(DoeCompilerTypeV4);zero.member_record_size=sizeof(DoeCompilerMemberV4);zero.resource_record_size=sizeof(DoeCompilerResourceV4);DoeCompilerTypedResult r;
 CHECK(invoke(c,aggregate_source,sizeof(aggregate_source)-1u,3u,&zero,&r)==DOE_COMPILER_OUTPUT_TOO_SMALL&&counts_equal(all,&r.required)&&r.required_is_exact==1u,"null zero spans discover exact requirements");
 Guard src;CHECK(guard_new(&src,sizeof(aggregate_source)-1u)==0,"non-NUL source");memcpy(src.data,aggregate_source,src.bytes);bulk_mark(&b);
 CHECK(invoke(c,(char *)src.data,src.bytes,3u,&b.o,&r)==DOE_COMPILER_OK,"non-NUL source accepted");
 unsigned char *copies[5];for(unsigned i=0;i<5u;++i){copies[i]=(unsigned char *)malloc(b.span[i].bytes);CHECK(copies[i]!=NULL,"ownership snapshot");memcpy(copies[i],b.span[i].data,b.span[i].bytes);}
 memset(src.data,0xcc,src.bytes);memset(c->workspace.data,0xdd,c->workspace.bytes);guard_free(&src);
 CHECK(reflection_shape(&b,&r)==0,"source/workspace overwrite leaves owned names");
 DoeCompilerTypedResult other;CHECK(invoke(c,old_source,sizeof(old_source)-1u,5u,NULL,&other)==DOE_COMPILER_OK,"workspace reusable after overwrite");
 for(unsigned i=0;i<5u;++i){CHECK(memcmp(copies[i],b.span[i].data,b.span[i].bytes)==0,"subsequent call does not retain outputs");free(copies[i]);}
 bulk_free(&b);return 0;
}
static int duplicate_names_and_tail_sentinels(Client *c){
 static const char source[]="struct First {cell:u32,} struct Second {cell:u32,} @group(0) @binding(0) var<storage,read_write> first:First; @group(0) @binding(1) var<storage,read_write> second:Second; @compute @workgroup_size(1) fn names(){first.cell=1u;second.cell=2u;}";
 DoeCompilerTypedResult r;CHECK(invoke(c,source,sizeof(source)-1u,7u,NULL,&r)==DOE_COMPILER_OK,"repeated member names admitted");Bulk b;CHECK(bulk_new(&b,&r.required)==0,"duplicate name outputs");CHECK(invoke(c,source,sizeof(source)-1u,3u,&b.o,&r)==DOE_COMPILER_OK,"duplicate name publication");
 CHECK(r.published.types==3u&&r.published.members==2u&&r.published.resources==2u,"nominal types distinct, common leaf shared");size_t cursor=0u;
 CHECK(consume_name(&b,r.module.entry_point,&cursor,"names")==0,"entry name once");CHECK(consume_name(&b,b.o.types[0].name,&cursor,"First")==0&&consume_name(&b,b.o.types[1].name,&cursor,"Second")==0,"named type order");
 CHECK(consume_name(&b,b.o.members[0].name,&cursor,"cell")==0&&consume_name(&b,b.o.members[1].name,&cursor,"cell")==0,"same spelling stored once per record, not interned");
 CHECK(b.o.members[0].name.offset!=b.o.members[1].name.offset,"distinct offsets for duplicate names");CHECK(consume_name(&b,b.o.resources[0].name,&cursor,"first")==0&&consume_name(&b,b.o.resources[1].name,&cursor,"second")==0,"resource names declaration ordered");CHECK(cursor==r.published.name_bytes,"duplicate names counted exactly");
 for(unsigned i=0;i<2u;++i){const DoeCompilerResourceV4 *p=&b.o.resources[i];CHECK(p->flags==0u&&p->runtime_array_type==UINT32_MAX&&p->runtime_array_offset_bytes==0u&&p->runtime_array_stride_bytes==0u,"absent runtime tail uses NONE and zeros");CHECK(p->group==0u&&p->binding==i&&p->kind==DOE_COMPILER_STORAGE_BUFFER&&p->access==DOE_COMPILER_READ_WRITE&&p->type_index==i&&p->reserved==0u,"resource declaration fields");}
 CHECK(b.o.types[2].name.offset==0u&&b.o.types[2].name.length==0u&&b.o.types[2].element_type==UINT32_MAX,"empty type name distinct from absent type index");bulk_free(&b);return 0;
}
static int query_matrix(void){
 for(unsigned kind=0;kind<30u;++kind){DoeCompilerTypedCapacities c=generous;DoeCompilerWorkspaceRequirements r;memset(&r,MARK,sizeof(r));
  unsigned version=4u;size_t cs=sizeof(c),rs=sizeof(r);const DoeCompilerTypedCapacities *cp=&c;DoeCompilerWorkspaceRequirements *rp=&r;DoeCompilerStatus expected=DOE_COMPILER_INPUT_ERROR;
  switch(kind){case 0:version=0u;expected=DOE_COMPILER_ABI_MISMATCH;break;case 1:--cs;cp=NULL;expected=DOE_COMPILER_ABI_MISMATCH;break;case 2:++rs;cp=NULL;expected=DOE_COMPILER_ABI_MISMATCH;break;case 3:rp=NULL;break;case 4:cp=NULL;break;case 5:c.reserved=1u;break;case 6:c.expression_depth=0u;break;case 7:c.control_depth=0u;break;case 8:c.type_syntax_depth=0u;break;case 9:c.expression_depth=65u;break;case 10:c.control_depth=33u;break;case 11:c.type_syntax_depth=65u;break;
   default:{size_t *fields[]={&c.source_bytes,&c.tokens,&c.statements,&c.expressions,&c.types,&c.members,&c.resources,&c.access_steps,&c.constants,&c.constant_edges,&c.ir_instructions,&c.ir_operands,&c.target_declarations,&c.traversal_items,&c.words};if(kind<27u)*fields[kind-12u]=SIZE_MAX;else if(kind==27u)c.source_bytes=UINT32_MAX;else if(kind==28u)c.types=UINT32_MAX;else c.expressions=UINT32_MAX;break;}}
  errno=17777;++native_inside;DoeCompilerStatus s=doe_compiler_typed_requirements(version,cp,cs,rp,rs);--native_inside;CHECK(errno==17777,"query errno");CHECK(s==expected&&bytes_are(&r,sizeof(r),MARK),"query error/no-write precedence");
 }
 DoeCompilerTypedCapacities z;memset(&z,0,sizeof(z));z.expression_depth=1u;z.control_depth=1u;z.type_syntax_depth=1u;DoeCompilerWorkspaceRequirements r;CHECK(query(&z,&r)==DOE_COMPILER_OK,"all zero logical pools admitted");return 0;
}
static int preflight(Client *c,const DoeCompilerTypedOutputCounts *n){Bulk b;CHECK(bulk_new(&b,n)==0,"preflight outputs");
 for(unsigned kind=0;kind<47u;++kind){DoeCompilerTypedCapacities caps=c->caps;DoeCompilerTypedOutputs o=b.o;DoeCompilerTypedResult r;memset(&r,MARK,sizeof(r));bulk_mark(&b);guard_mark(&c->workspace);
  const DoeCompilerTypedCapacities *cp=&caps;const DoeCompilerTypedOutputs *op=&o;DoeCompilerTypedResult *rp=&r;const char *source=aggregate_source;size_t length=sizeof(aggregate_source)-1u,cs=sizeof(caps),os=sizeof(o),rs=sizeof(r),ws=c->req.size_bytes;void *workspace=c->workspace.data;unsigned version=4u,flags=3u;DoeCompilerStatus expected=DOE_COMPILER_INPUT_ERROR;int untouched=0,work_untouched=0;
  switch(kind){case 0:rp=NULL;untouched=work_untouched=1;version=99u;break;case 1:version=99u;expected=DOE_COMPILER_ABI_MISMATCH;break;case 2:--cs;cp=NULL;expected=DOE_COMPILER_ABI_MISMATCH;break;case 3:--rs;expected=DOE_COMPILER_ABI_MISMATCH;break;case 4:--os;expected=DOE_COMPILER_ABI_MISMATCH;break;case 5:op=NULL;expected=DOE_COMPILER_ABI_MISMATCH;break;case 6:o.type_record_size=0u;expected=DOE_COMPILER_ABI_MISMATCH;break;case 7:++o.member_record_size;expected=DOE_COMPILER_ABI_MISMATCH;break;case 8:--o.resource_record_size;expected=DOE_COMPILER_ABI_MISMATCH;break;case 9:cp=NULL;break;case 10:flags=0u;break;case 11:flags=4u;op=NULL;os=0u;break;case 12:flags=8u;break;case 13:flags=7u;break;case 14:op=NULL;os=0u;break;case 15:flags=1u;break;case 16:flags=2u;break;case 17:workspace=NULL;work_untouched=1;break;case 18:--ws;work_untouched=1;break;case 19:workspace=c->workspace.data+1u;--ws;work_untouched=1;if(c->req.alignment_bytes==1u)continue;break;case 20:source=NULL;break;case 21:o.words=NULL;break;case 22:o.types=NULL;break;case 23:o.members=NULL;break;case 24:o.resources=NULL;break;case 25:o.names=NULL;break;case 26:caps.reserved=1u;break;case 27:caps.types=SIZE_MAX;break;case 28:o.type_capacity=SIZE_MAX;o.types=NULL;break;case 29:o.member_capacity=SIZE_MAX;o.members=NULL;break;case 30:o.resource_capacity=SIZE_MAX;o.resources=NULL;break;case 31:o.word_capacity=SIZE_MAX;o.words=NULL;break;
   case 32:if(_Alignof(uint32_t)==1u)continue;o.words=(uint32_t *)(void *)(b.span[0].data+1u);--o.word_capacity;break;
   case 33:if(_Alignof(DoeCompilerTypeV4)==1u)continue;o.types=(DoeCompilerTypeV4 *)(void *)(b.span[1].data+1u);--o.type_capacity;break;
   case 34:if(_Alignof(DoeCompilerMemberV4)==1u)continue;o.members=(DoeCompilerMemberV4 *)(void *)(b.span[2].data+1u);--o.member_capacity;break;
   case 35:if(_Alignof(DoeCompilerResourceV4)==1u)continue;o.resources=(DoeCompilerResourceV4 *)(void *)(b.span[3].data+1u);o.resource_capacity=1u;break;
   case 36:case 37:case 38:memset(&o,0,sizeof(o));o.type_record_size=sizeof(DoeCompilerTypeV4);o.member_record_size=sizeof(DoeCompilerMemberV4);o.resource_record_size=sizeof(DoeCompilerResourceV4);if(kind==36u)o.type_record_size=0u;if(kind==37u)o.member_record_size=0u;if(kind==38u)o.resource_record_size=0u;expected=DOE_COMPILER_ABI_MISMATCH;break;
   case 40:flags=9u;break;case 41:flags=10u;break;case 42:flags=11u;break;case 43:flags=13u;op=NULL;os=0u;break;case 44:flags=14u;op=NULL;os=0u;break;case 45:flags=15u;op=NULL;os=0u;break;case 46:flags=UINT32_MAX;op=NULL;os=0u;break;
   case 39:source="@compute\0 @workgroup_size(1) fn hidden(){}";length=sizeof("@compute\0 @workgroup_size(1) fn hidden(){}")-1u;break;
   default:return 1;}
  errno=18888;++native_inside;DoeCompilerStatus st=doe_compiler_compile_typed(version,cp,cs,workspace,ws,source,length,flags,op,os,rp,rs);--native_inside;
  CHECK(errno==18888&&st==expected,"compile preflight status and errno");CHECK(bulk_unchanged(&b)&&guard_ok(&c->workspace),"preflight all bulk unchanged and redzones");
  if(st==DOE_COMPILER_ABI_MISMATCH)untouched=work_untouched=1;
  if(untouched)CHECK(bytes_are(&r,sizeof(r),MARK),"preflight result no-write");else CHECK(failure_result(&r,st,0)==0,"ordinary preflight clears result");
  if(work_untouched)CHECK(bytes_are(c->workspace.data,c->workspace.bytes,MARK),"preflight before workspace touch");
  CHECK(invoke(c,aggregate_source,sizeof(aggregate_source)-1u,3u,&b.o,&r)==DOE_COMPILER_OK,"reuse after every preflight outcome");
 }
 bulk_free(&b);return 0;
}
static int language_failures(Client *c,const DoeCompilerTypedOutputCounts *n){Bulk b;CHECK(bulk_new(&b,n)==0,"failure outputs");const char *sources[]={parse_source,semantic_source,unsupported_source};const DoeCompilerStatus expected[]={DOE_COMPILER_PARSE_ERROR,DOE_COMPILER_SEMANTIC_ERROR,DOE_COMPILER_UNSUPPORTED};
 for(unsigned i=0;i<3u;++i)for(unsigned f=1u;f<=7u;++f){if(f==4u)continue;bulk_mark(&b);DoeCompilerTypedOutputs o=selected(&b,f);DoeCompilerTypedResult r;CHECK(invoke(c,sources[i],strlen(sources[i]),f,(f&4u)?NULL:&o,&r)==expected[i],"same validation in every mode");CHECK(failure_result(&r,expected[i],0)==0&&bulk_unchanged(&b),"language failure atomic");CHECK(invoke(c,aggregate_source,sizeof(aggregate_source)-1u,3u,&b.o,&r)==DOE_COMPILER_OK,"reuse after semantic/parse/unsupported failure");}
 bulk_free(&b);return 0;}
int main(void){CHECK(native_allocator_control()==0,"allocator interception positive control");Client c;CHECK(query_matrix()==0,"query matrix");CHECK(client_new(&c,&generous)==0,"native client");DoeCompilerTypedOutputCounts n;CHECK(mode_matrix(&c,&n)==0,"mode/count/ownership matrix");CHECK(duplicate_names_and_tail_sentinels(&c)==0,"duplicate names and null/sentinel distinction");CHECK(preflight(&c,&n)==0,"preflight matrix");CHECK(language_failures(&c,&n)==0,"language failure matrix");CHECK(guard_ok(&c.workspace),"workspace redzones");client_free(&c);CHECK(atomic_load(&native_allocations)==0u,"no library allocation/free");puts("PASS ABI4 flags, exact counts, all-span atomicity, preflight, ownership, reuse");return 0;}
