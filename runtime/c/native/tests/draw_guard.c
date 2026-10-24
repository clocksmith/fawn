/* SPDX-License-Identifier: MIT
 * Execute the actual shared WGSL on the selected public backend. Small bounded
 * windows exercise large logical ordinals; no huge allocation/device claim. */
#include "consumer.h"
#include "../../common/include/doe_draw_guard.h"

typedef struct guard_case {
    uint32_t record[10], indices[4], params[9];
    bool scan, normal_noop, index_noop;
} guard_case;

static void run_case(state *s,WGPUComputePipeline normal,WGPUComputePipeline scan,const guard_case *c)
{
    WGPUBuffer record=buffer(s,sizeof(c->record),WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst|WGPUBufferUsage_CopySrc);
    WGPUBuffer indices=buffer(s,sizeof(c->indices),WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst|WGPUBufferUsage_CopySrc);
    WGPUBuffer params=buffer(s,sizeof(c->params),WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst);
    WGPUBuffer readback=buffer(s,sizeof(c->record)+sizeof(c->indices),WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,record,0,c->record,sizeof(c->record));
    wgpuQueueWriteBuffer(s->queue,indices,0,c->indices,sizeof(c->indices));
    wgpuQueueWriteBuffer(s->queue,params,0,c->params,sizeof(c->params));
    WGPUBindGroupEntry entries[3]={{.binding=0,.buffer=record,.size=sizeof(c->record)},
        {.binding=1,.buffer=indices,.size=sizeof(c->indices)},
        {.binding=2,.buffer=params,.size=sizeof(c->params)}};
    WGPUBindGroup a=group(s,normal,entries,1),b=NULL;
    if (c->scan) b=group(s,scan,entries,3);
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s->device,NULL);
    dispatch(encoder,normal,a);
    if (c->scan) dispatch(encoder,scan,b);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,record,0,readback,0,sizeof(c->record));
    wgpuCommandEncoderCopyBufferToBuffer(encoder,indices,0,readback,sizeof(c->record),sizeof(c->indices));
    wgpuBindGroupRelease(a);
    if (b) wgpuBindGroupRelease(b);
    wgpuBufferRelease(record); wgpuBufferRelease(indices); wgpuBufferRelease(params);
    submit(s,encoder);
    const uint32_t *actual=map(s,readback,sizeof(c->record)+sizeof(c->indices));
    for (size_t i=0;i<10;++i) {
        uint32_t expected=c->normal_noop && i<((c->record[7]&1) ? 5u : 4u) ? 0 :
            c->index_noop && i<2 ? 0 : c->record[i];
        if (actual[i]!=expected) fprintf(stderr,"draw guard word%zu: %08x != %08x\n",i,actual[i],expected);
        assert(actual[i]==expected);
    }
    assert(!memcmp(actual+10,c->indices,sizeof(c->indices)));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

int main(void)
{
    state s={0};
    consumer_init(&s);
    char source[DOE_DRAW_INDEX_SCAN_SOURCE_CAPACITY];
    size_t length=0;
    assert(doe_draw_argument_source_v2(source,sizeof(source),&length)==DOE_DRAW_GUARD_SOURCE_OK);
    WGPUComputePipeline normal=pipeline(&s,source);
    assert(doe_draw_index_scan_source(source,sizeof(source),&length)==DOE_DRAW_GUARD_SOURCE_OK);
    WGPUComputePipeline scan=pipeline(&s,source);
    static const guard_case cases[]={
        {.record={2,1,UINT32_MAX,0,1,1,0,2,1,0}}, /* End2^32+1 exactly fits. */
        {.record={2,1,UINT32_MAX,0,0,1,0,2,1,0},.normal_noop=true},
        {.record={1,1,UINT32_MAX,0,0,0,1,3,1,0}}, /* Last index at2^32-1. */
        {.record={UINT32_MAX,1,UINT32_MAX,0,0,UINT32_MAX-1,1,3,1,0}}, /* End2^33-2. */
        {.record={1,1,0,UINT32_MAX,1,0,0,2,0,1}}, /* Instance end2^32. */
        {.record={1,1,0,0,1,1,2,1,0,0},.normal_noop=true}, /* FirstInstance feature absent. */
        {.record={0,1,3,0,0,4,1,3,0,0},.normal_noop=true},
        {.record={3,1,0,0,0,3,1,3,0,0},.indices={0x0001beef,0x00030002},
            .params={0,0,3,1,1,4,0,0,64},.scan=true}, /* Odd uint16 prefix/tail. */
        {.record={3,1,0,0,0,3,1,3,0,0},.indices={0x0001beef,0x00030002},
            .params={0,0,3,1,1,3,0,0,64},.scan=true,.index_noop=true},
        {.record={2,1,0,0,0,2,1,3,0,0},.indices={0x0002ffff},
            .params={0,0,2,0,1,3,0,1,64},.scan=true}, /* Strip restart is not a vertex. */
        {.record={2,1,0,0,0,2,1,3,0,0},.indices={0x0002ffff},
            .params={0,0,2,0,1,3,0,0,64},.scan=true,.index_noop=true},
        {.record={3,1,0,0xfffffffdu,0,3,1,3,0,0},.indices={3,4,5},
            .params={0,0,3,0,0,3,0,0,64},.scan=true},
        {.record={1,1,0,0x80000000u,0,1,1,3,0,0},.indices={0x80000000u},
            .params={0,0,1,0,0,1,0,0,64},.scan=true}, /* INT_MIN magnitude is unsigned. */
        {.record={1,1,0,0x80000000u,0,1,1,3,0,0},.indices={1},
            .params={0,0,1,0,0,1,0,0,64},.scan=true,.index_noop=true},
        {.record={1,1,0,1,0,1,1,3,0,0},.indices={UINT32_MAX},
            .params={0,0,1,0,0,1,1,0,64},.scan=true}, /* Positive base carries into highword. */
        {.record={1,1,0,1,0,1,1,3,0,0},.indices={UINT32_MAX},
            .params={0,0,1,0,0,0,1,0,64},.scan=true,.index_noop=true},
        {.record={2,1,UINT32_MAX,0,0,1,1,3,1,0},.indices={0,1},
            .params={UINT32_MAX,0,2,0,0,2,0,0,64},.scan=true}, /* Ordinal window crosses2^32. */
        {.record={1,1,0,0,0,1,1,3,0,0},.indices={7,8,9,10},
            .params={10,1,4,0,0,0,0,0,64},.scan=true}, /* Unrelated window cannot cancel. */
        {.record={1,1,2,0,0,4,1,3,0,0},.indices={99,99,1,99},
            .params={0,0,4,0,0,2,0,0,64},.scan=true}, /* Scan only the middle requested element. */
        {.record={2,1,0,0,0,2,1,3,0,0},.indices={99,99,99,99},
            .params={2,0,4,0,0,1,0,0,64},.scan=true}, /* Request ends exactly at this window. */
        {.record={2,1,UINT32_MAX,0,0,1,1,3,1,0},.indices={1,99,99,99},
            .params={0,1,4,0,0,2,0,0,64},.scan=true}, /* High window includes only ordinal2^32. */
        {.record={2,1,UINT32_MAX,0,0,1,1,3,1,0},.indices={9,99,99,99},
            .params={0,1,4,0,0,2,0,0,64},.scan=true,.index_noop=true},
        {.record={2,1,UINT32_MAX,0,0,1,1,3,1,0},.indices={99,99,99,99},
            .params={1,1,4,0,0,1,0,0,64},.scan=true} /* High request end equals window start. */
    };
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) run_case(&s,normal,scan,&cases[i]);
    wgpuComputePipelineRelease(normal); wgpuComputePipelineRelease(scan);
    consumer_finish(&s);
    printf("PASS shared draw guards: actual WGSL V2 normalization and atomic index scan, 23 exact records,64-bit capacities/ordinals, exact window intersections, signed base/INT_MIN, odd uint16 windows/restart, unchanged source words and early releases %s\n",adapter_class_scope());
}
