/* Independent postdisclosure direct private boundary proof, not 2^32-ID compilation. */
#include "spirv.c"
#include <stdio.h>
bool source_error(Source *s, DoeCompilerStatus status, size_t offset, const char *message) {
    s->diagnostic->status=status;s->diagnostic->offset=offset;
    (void)snprintf(s->diagnostic->message,sizeof(s->diagnostic->message),"%s",message);
    return false;
}
int main(void) {
    DoeCompilerDiagnostic d={0}; Source s={.length=17u,.diagnostic=&d};
    Emitter e={.source=&s,.next_id=UINT32_MAX-3u,.id_bound=UINT32_MAX-1u};
    if(reserve_ids(&e,2u)!=UINT32_MAX-3u || e.next_id!=UINT32_MAX-1u || e.failed)return 1;
    if(reserve_ids(&e,1u)!=0u || !e.failed || d.status!=DOE_COMPILER_UNSUPPORTED || d.offset!=17u || strstr(d.message,"format")==NULL)return 2;
    const uint32_t next=e.next_id;const DoeCompilerDiagnostic old=d;
    if(reserve_ids(&e,0u)!=0u || e.next_id!=next || memcmp(&old,&d,sizeof(d))!=0)return 3;
    e=(Emitter){.source=&s,.next_id=61u,.id_bound=63u};d=(DoeCompilerDiagnostic){0};
    if(reserve_ids(&e,2u)!=61u || e.next_id!=63u || e.failed)return 4;
    if(reserve_ids(&e,1u)!=0u || !e.failed || d.status!=DOE_COMPILER_INPUT_ERROR || strstr(d.message,"format")!=NULL)return 5;
    puts("PASS exact target-ID format boundary, nonsaturated internal envelope, sticky failure and unchanged counter");return 0;
}
