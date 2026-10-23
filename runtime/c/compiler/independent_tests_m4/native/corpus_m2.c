#include "doe_compiler.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t calls;
static void check(const char *s,size_t n,size_t cap,int expected) {
 uint32_t w[4098]; for(size_t i=0;i<4098;i++)w[i]=0xa55a36c9;
 DoeCompilerResult r;memset(&r,0xcd,sizeof(r));
 DoeCompilerStatus status=doe_compiler_compile(1,s,n,w+1,cap,&r,sizeof(r));calls++;
 assert(status==r.diagnostic.status);assert(w[0]==0xa55a36c9);
 for(size_t i=cap+1;i<4098;i++)assert(w[i]==0xa55a36c9);
 if(expected==0)assert(status==DOE_COMPILER_OK);
 if(expected==1)assert(status!=DOE_COMPILER_OK);
 if(status){assert(r.word_count==0);assert(r.diagnostic.offset<=n);assert(r.diagnostic.line && r.diagnostic.column);for(size_t i=0;i<4098;i++)assert(w[i]==0xa55a36c9);const unsigned char *p=(const unsigned char *)&r.reflection;for(size_t i=0;i<sizeof(r.reflection);i++)assert(p[i]==0);}
 else assert(r.word_count>0 && r.word_count<=cap);
}
int main(int argc,char **argv) {
 for(int i=1;i<argc;i++){
  FILE *f=fopen(argv[i],"rb");assert(f);assert(fseek(f,0,SEEK_END)==0);long size=ftell(f);assert(size>=0);rewind(f);
  size_t n=(size_t)size;char *s=malloc(n?n:1);assert(s);assert(fread(s,1,n,f)==n);assert(fclose(f)==0);
  const char *name=strrchr(argv[i],'/');name=name?name+1:argv[i];int expect=strncmp(name,"positive_",9)?1:0;
  check(s,n,4096,expect);
  if(n<4096){for(size_t p=0;p<n;p++)check(s,p,4096,-1);for(size_t p=0;p<n;p++){char old=s[p];s[p]=0;check(s,n,4096,1);s[p]=old;}}
  check(s,n,0,1);check(s,n,1,1);check(s,n,4095,-1);free(s);
 }
 printf("M2 frozen corpus sanitizer checks: %zu calls across %d fixtures passed\n",calls,argc-1);return 0;
}
