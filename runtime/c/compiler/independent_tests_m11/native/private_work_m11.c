/* Post-handoff independent private wrapper, never added to candidate exports. */
#include "support_m11.h"
#include "sema.h"
#include <inttypes.h>
static uint64_t override_limit=UINT64_MAX,observed_work;
static uint32_t observed_expressions,observed_statements,observed_ir,observed_types,observed_roots;
static uint64_t observed_shared;
static unsigned protected_changed;
static size_t current_workspace_size;
static uint64_t hash_bytes(const void *p,size_t n){const unsigned char *b=p;uint64_t h=UINT64_C(1469598103934665603);for(size_t i=0;i<n;i++){h^=b[i];h*=UINT64_C(1099511628211);}return h;}
bool __real_uniformity_analyze(Source *,uint64_t,uint64_t *);
bool __wrap_uniformity_analyze(Source *source,uint64_t limit,uint64_t *used);
bool __wrap_uniformity_analyze(Source *source,uint64_t limit,uint64_t *used){
 CompilerWorkspace *c=source->compiler;
 uint64_t before[5]={hash_bytes(c->instructions,(size_t)c->instruction_count*sizeof(*c->instructions)),hash_bytes(c->types,(size_t)c->type_count*sizeof(*c->types)),hash_bytes(c->resources,(size_t)c->resource_count*sizeof(*c->resources)),hash_bytes(c->accesses,(size_t)c->access_count*sizeof(*c->accesses)),hash_bytes(c->typed_operands,(size_t)c->operand_count*sizeof(*c->typed_operands))};
 size_t offset=(size_t)(c->uniformity-(unsigned char*)c);if(offset>=current_workspace_size){protected_changed=1u;return false;}
 memset(c->uniformity,0xbd,current_workspace_size-offset);
 bool ok=__real_uniformity_analyze(source,override_limit==UINT64_MAX?limit:override_limit,&observed_work);if(used)*used=observed_work;
 uint64_t after[5]={hash_bytes(c->instructions,(size_t)c->instruction_count*sizeof(*c->instructions)),hash_bytes(c->types,(size_t)c->type_count*sizeof(*c->types)),hash_bytes(c->resources,(size_t)c->resource_count*sizeof(*c->resources)),hash_bytes(c->accesses,(size_t)c->access_count*sizeof(*c->accesses)),hash_bytes(c->typed_operands,(size_t)c->operand_count*sizeof(*c->typed_operands))};
 if(memcmp(before,after,sizeof(before)))protected_changed=1u;
 observed_expressions=c->expression_count;observed_statements=c->statement_count;observed_ir=c->instruction_count;observed_types=c->type_count;observed_roots=c->workgroup_count;observed_shared=c->workgroup_storage_bytes;return ok;
}
static int one(const char *path){
 FILE *f=fopen(path,"rb");CHECK(f!=NULL,"open frozen source");CHECK(fseek(f,0,SEEK_END)==0,"source seek");long z=ftell(f);CHECK(z>=0&&z<65536,"source length");rewind(f);char *s=malloc((size_t)z+1u);CHECK(s!=NULL,"read source allocation");CHECK(fread(s,1u,(size_t)z,f)==(size_t)z&&fclose(f)==0,"read source");s[z]=0;
 Client c;CHECK(client_new(&c,&generous)==0,"private client");current_workspace_size=c.req.size_bytes;DoeCompilerTypedResult r;override_limit=UINT64_MAX;CHECK(invoke(&c,s,(size_t)z,7u,NULL,&r)==DOE_COMPILER_OK,"frozen positive public compilation");uint64_t n=observed_work;Bulk b;CHECK(bulk_new(&b,&r.required)==0,"private outputs");unsigned modes[]={1u,2u,3u,5u,6u,7u};
 for(unsigned i=0;i<COUNT(modes);i++){unsigned mode=modes[i];DoeCompilerTypedOutputs o=selected(&b,mode);override_limit=n;bulk_mark(&b);CHECK(invoke(&c,s,(size_t)z,mode,(mode&4u)?NULL:&o,&r)==DOE_COMPILER_OK,"exact analysis budget accepted through every public mode");CHECK(observed_work==n,"all modes count same exact work");CHECK(guard_ok(&c.workspace)&&bulk_guards(&b),"exact work span redzones");
 if(n){override_limit=n-1u;bulk_mark(&b);CHECK(invoke(&c,s,(size_t)z,mode,(mode&4u)?NULL:&o,&r)==DOE_COMPILER_UNSUPPORTED,"N-1 is capability exhaustion, not semantic failure");CHECK(observed_work<=n-1u,"charge checks before crossing limit");CHECK(failure_result(&r,DOE_COMPILER_UNSUPPORTED,0)==0&&bulk_unchanged(&b),"N-1 atomic unpublished");CHECK(r.exhausted_pool==0u&&r.minimum_pool_capacity==0u,"work limit is not logical pool");CHECK(strcmp(r.diagnostic.message,"uniformity analysis work limit exceeded")==0&&r.diagnostic.offset<(size_t)z,"work limit exact original diagnostic");}}
 override_limit=UINT64_MAX;CHECK(invoke(&c,s,(size_t)z,3u,&b.o,&r)==DOE_COMPILER_OK,"reuse after work exhaustion");CHECK(protected_changed==0u,"uniformity does not mutate persistent typed IR/types/resources/accesses/operands");
 const uint32_t bound=b.o.words[3];const uint64_t g=c.caps.tokens/8u,ids=256u+c.caps.target_declarations+64u*c.caps.ir_instructions+3u*(c.caps.tokens/6u)+c.caps.tokens/4u+c.caps.statements+1u+11u*c.caps.access_steps+c.caps.resources+g+8u;
 const uint64_t words=2048u+256u*c.caps.ir_instructions+32u*(c.caps.tokens/6u)+4u*(c.caps.tokens/4u)+c.caps.ir_operands+9u*c.caps.target_declarations+6u*c.caps.members+c.caps.constant_edges+4u*c.caps.constants+57u*c.caps.access_steps+40u*c.caps.resources+25u*g+64u;
 CHECK(bound<=ids&&r.published.words<=words,"target counts within frozen conservative envelope");
 printf("%s work=%"PRIu64" expressions=%u statements=%u ir=%u types=%u roots=%u logical_shared=%"PRIu64" bound=%u words=%zu limit_controls=%s modes=6 poisoned_secondary=1\n",path,n,observed_expressions,observed_statements,observed_ir,observed_types,observed_roots,observed_shared,bound,r.published.words,n?"N_and_N-1":"no_barrier_zero_work");
 bulk_free(&b);client_free(&c);free(s);return 0;
}
int main(int argc,char **argv){CHECK(native_allocator_control()==0,"allocator control");for(int i=1;i<argc;i++)CHECK(one(argv[i])==0,"private work case");CHECK(atomic_load(&native_allocations)==0u,"public compile windows no allocations");return 0;}
