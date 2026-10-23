#include "doe_compiler.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned rng=0xD0E2026u;
static unsigned next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static const char seed[]="@group(0) @binding(0) var<storage,read_write> data: array<f32>; @compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gid: vec3u) { let i=gid.x; let saved: f32=data[i]; data[i]=0.1; if (i==0u) { let v=max(saved*2.0, 1.0); data[i]=v-0.5; return; } data[i]=saved; }";
static void check(const char *source,size_t len,size_t cap) {
    uint32_t words[4098];for(size_t i=0;i<4098;i++)words[i]=0xA55A36C9u;
    DoeCompilerResult r;memset(&r,0xCD,sizeof(r));
    DoeCompilerStatus status=doe_compiler_compile(1,source,len,words+1,cap,&r,sizeof(r));
    assert(status==r.diagnostic.status);assert(words[0]==0xA55A36C9u);
    for(size_t i=1+cap;i<4098;i++)assert(words[i]==0xA55A36C9u);
    if(status!=DOE_COMPILER_OK) {
        assert(r.word_count==0);assert(r.diagnostic.offset<=len);assert(r.diagnostic.line>0 && r.diagnostic.column>0);
        for(size_t i=0;i<4098;i++)assert(words[i]==0xA55A36C9u);
        const unsigned char *p=(const unsigned char *)&r.reflection;
        for(size_t i=0;i<sizeof(r.reflection);i++)assert(p[i]==0);
        assert(memchr(r.diagnostic.message,0,sizeof(r.diagnostic.message))!=NULL);
    }else assert(r.word_count>0 && r.word_count<=cap);
}
int main(void) {
    for(unsigned i=0;i<50000;i++) {
        size_t len=(i%3==0)?next()%512:sizeof(seed)-1;
        char *s=malloc(len?len:1);assert(s);
        if(i%3==0)for(size_t j=0;j<len;j++)s[j]=(char)next();
        else {
            memcpy(s,seed,len);
            unsigned mutations=1+next()%8;
            for(unsigned j=0;j<mutations;j++)s[next()%len]=(char)next();
        }
        check(s,len,next()%4097);free(s);
    }
    for(size_t n=0;n<sizeof(seed)-1;n++){char *s=malloc(n?n:1);assert(s);memcpy(s,seed,n);check(s,n,4096);free(s);}
    for(size_t n=65535;n<=65537;n++){char *s=malloc(n);assert(s);memset(s,' ',n);memcpy(s,seed,sizeof(seed)-1);check(s,n,4096);free(s);}
    puts("independent sanitizer harness: 50000 deterministic mutations/random byte cases plus truncated prefixes and source boundaries passed");return 0;
}
