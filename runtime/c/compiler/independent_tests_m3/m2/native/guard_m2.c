#define _GNU_SOURCE
#include "doe_compiler.h"
#include <dlfcn.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Linux/glibc test-process-only allocator interposition. Not production code. */
extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t,size_t);
extern void *__libc_realloc(void *,size_t);
extern void __libc_free(void *);
static volatile unsigned gate, allocations, deallocations;
void *malloc(size_t n) { if(gate) ++allocations;return __libc_malloc(n); }
void *calloc(size_t n,size_t s) { if(gate) ++allocations;return __libc_calloc(n,s); }
void *realloc(void *p,size_t n) { if(gate) ++allocations;return __libc_realloc(p,n); }
void free(void *p) { if(gate) ++deallocations;__libc_free(p); }

typedef DoeCompilerStatus (*Compile)(uint32_t,const char *,size_t,uint32_t *,size_t,DoeCompilerResult *,size_t);
static const char valid[]="@group(0) @binding(0) var<storage, read> input_values: array<u32>;\n@group(0) @binding(1) var<storage, read_write> output_values: array<u32>;\n\n@compute @workgroup_size(64)\nfn main(@builtin(global_invocation_id) gid: vec3u) {\n  let index = gid.x;\n  if (index == 0u) {\n    output_values[index] = input_values[index];\n    return;\n  }\n  output_values[index] = input_values[index - 1u] + input_values[index];\n}\n";
static int failures;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);++failures;}}while(0)
static DoeCompilerStatus invoke(Compile f,const char *s,size_t n,uint32_t *w,size_t cap,DoeCompilerResult *r) {
    gate=1;DoeCompilerStatus st=f(DOE_COMPILER_ABI_VERSION,s,n,w,cap,r,sizeof(*r));gate=0;return st;
}
int main(int argc,char **argv) {
    if(argc!=2){fprintf(stderr,"usage: %s shared-library\n",argv[0]);return 2;}
    void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){fprintf(stderr,"%s\n",dlerror());return 2;}
    Compile compile=(Compile)dlsym(lib,"doe_compiler_compile");if(!compile){fprintf(stderr,"symbol missing\n");return 2;}
    size_t page=(size_t)sysconf(_SC_PAGESIZE),length=sizeof(valid)-1;
    char *mapping=mmap(NULL,page*18,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(mapping!=MAP_FAILED);if(mapping==MAP_FAILED)return 2;
    CHECK(mprotect(mapping+page*17,page,PROT_NONE)==0);
    char *end=mapping+page*17;
    DoeCompilerResult result;uint32_t words[DOE_COMPILER_MAX_WORDS+2];
    memcpy(end-length,valid,length);memset(words,0xA5,sizeof(words));
    CHECK(invoke(compile,end-length,length,words+1,DOE_COMPILER_MAX_WORDS,&result)==DOE_COMPILER_OK);
    CHECK(result.word_count>0 && result.word_count<=DOE_COMPILER_MAX_WORDS);size_t count=result.word_count;
    CHECK(words[0]==0xA5A5A5A5 && words[DOE_COMPILER_MAX_WORDS+1]==0xA5A5A5A5);
    memset(end-65536,' ',65536);memcpy(end-65536,valid,length);
    CHECK(invoke(compile,end-65536,65536,words+1,DOE_COMPILER_MAX_WORDS,&result)==DOE_COMPILER_OK);
    memset(end-65537,' ',65537);memcpy(end-65537,valid,length);
    CHECK(invoke(compile,end-65537,65537,words+1,DOE_COMPILER_MAX_WORDS,&result)==DOE_COMPILER_INPUT_ERROR);
    CHECK(result.word_count==0);
    memcpy(end-length,valid,length);
    char *out=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(out!=MAP_FAILED);if(out==MAP_FAILED)return 2;
    CHECK(mprotect(out+page,page,PROT_NONE)==0);
    uint32_t *exact=(uint32_t *)(out+page-count*4);memset(exact,0xA5,count*4);
    CHECK(invoke(compile,end-length,length,exact,count,&result)==DOE_COMPILER_OK);
    uint32_t *short_output=(uint32_t *)(out+page-(count-1)*4);memset(short_output,0xA5,(count-1)*4);
    CHECK(invoke(compile,end-length,length,short_output,count-1,&result)==DOE_COMPILER_OUTPUT_TOO_SMALL);
    for(size_t i=0;i<count-1;++i)CHECK(short_output[i]==0xA5A5A5A5);
    CHECK(invoke(compile,end-length,length,NULL,0,&result)==DOE_COMPILER_OUTPUT_TOO_SMALL);
    CHECK(invoke(compile,NULL,0,words+1,DOE_COMPILER_MAX_WORDS,&result)!=DOE_COMPILER_OK);
    CHECK(invoke(compile,NULL,1,words+1,DOE_COMPILER_MAX_WORDS,&result)==DOE_COMPILER_INPUT_ERROR);
    size_t semantic_end=length;while(semantic_end && isspace((unsigned char)valid[semantic_end-1]))--semantic_end;
    for(size_t n=0;n<length;++n) {
        memcpy(end-n,valid,n);DoeCompilerStatus st=invoke(compile,end-n,n,words+1,DOE_COMPILER_MAX_WORDS,&result);
        if(n<semantic_end){CHECK(st!=DOE_COMPILER_OK);CHECK(result.word_count==0);}
        else CHECK(st==DOE_COMPILER_OK);
    }
    memcpy(end-length,valid,length);
    for(int i=0;i<1000;++i)CHECK(invoke(compile,end-length,length,words+1,DOE_COMPILER_MAX_WORDS,&result)==DOE_COMPILER_OK);

    memset(&result,0xCD,sizeof(result));memset(words,0xA5,sizeof(words));
    unsigned char saved[sizeof(result)];memcpy(saved,&result,sizeof(saved));
    CHECK(compile(0,end-length,length,words+1,DOE_COMPILER_MAX_WORDS,&result,sizeof(result))==DOE_COMPILER_ABI_MISMATCH);
    CHECK(memcmp(saved,&result,sizeof(result))==0);
    CHECK(compile(1,end-length,length,words+1,DOE_COMPILER_MAX_WORDS,&result,sizeof(result)-1)==DOE_COMPILER_ABI_MISMATCH);
    CHECK(memcmp(saved,&result,sizeof(result))==0);
    CHECK(compile(1,end-length,length,words+1,DOE_COMPILER_MAX_WORDS,NULL,sizeof(result))==DOE_COMPILER_INPUT_ERROR);
    for(size_t i=0;i<DOE_COMPILER_MAX_WORDS+2;i++)CHECK(words[i]==0xA5A5A5A5);
    /* ABI mismatch must not dereference source/result/words in protected pages. */
    CHECK(compile(0,end,1,(uint32_t *)(out+page),1,(DoeCompilerResult *)(out+page),sizeof(result))==DOE_COMPILER_ABI_MISMATCH);
    CHECK(compile(1,end,1,(uint32_t *)(out+page),1,(DoeCompilerResult *)(out+page),sizeof(result)-1)==DOE_COMPILER_ABI_MISMATCH);
    CHECK(allocations==0);CHECK(deallocations==0);
    printf("guard-page checks: %s; truncated prefixes=%zu; repeated calls=1000; allocator calls during compile=%u; free calls=%u\n",failures?"FAIL":"PASS",length,allocations,deallocations);
    munmap(out,page*2);munmap(mapping,page*18);dlclose(lib);return failures?1:0;
}
