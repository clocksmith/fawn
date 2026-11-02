/* SPDX-License-Identifier: MIT */
#include "doe_draw_guard.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    char source[DOE_DRAW_ARGUMENT_BATCH_SOURCE_CAPACITY];
    memset(source,0x5a,sizeof(source));
    char saved[sizeof(source)]; memcpy(saved,source,sizeof(source));
    size_t length=37;
    assert(doe_draw_argument_source_batch(source,1,&length)==DOE_DRAW_GUARD_SOURCE_TOO_SMALL);
    assert(length==37 && !memcmp(source,saved,sizeof(source)));
    assert(doe_draw_argument_source_v2_batch(source,1,&length)==DOE_DRAW_GUARD_SOURCE_TOO_SMALL);
    assert(length==37 && !memcmp(source,saved,sizeof(source)));
    assert(doe_draw_argument_source_batch(source,sizeof(source),&length)==DOE_DRAW_GUARD_SOURCE_OK);
    assert(length==strlen(source) && strstr(source,"arrayLength(&record)/8u") && strstr(source,"gid.x*8u"));
    assert(doe_draw_argument_source_v2_batch(source,sizeof(source),&length)==DOE_DRAW_GUARD_SOURCE_OK);
    assert(length==strlen(source) && strstr(source,"arrayLength(&record)/10u") && strstr(source,"gid.x*10u"));
    assert(strstr(source,"record[base+8u]") && strstr(source,"record[base+9u]"));
    puts("Draw guard sources: explicit8/10-word batch construction, bounds and untouched failure spans PASS; no GPU execution");
    return 0;
}
