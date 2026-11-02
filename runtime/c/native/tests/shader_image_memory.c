/* SPDX-License-Identifier: MIT
 * Real readonly mappings and loader admission; no GPU or file-image substitute. */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static size_t allocation_calls, allocation_size, reject_at;
static bool reject_allocation;
static void *image_test_malloc(size_t size)
{
    ++allocation_calls;
    allocation_size=size;
    return reject_allocation || allocation_calls==reject_at ? NULL : malloc(size);
}
#define malloc image_test_malloc
#include "../src/shader_cache_identity.c"
#undef malloc
#if defined(NATIVE_IMAGE_ASAN)
#include <sanitizer/asan_interface.h>
#endif

static void unchanged_rejection(image_hash initial, uintptr_t base, uintptr_t address, size_t size)
{
    image_hash result=initial;
    size_t allocations=allocation_calls;
    assert(!span(&result,base,address,size));
    assert(!memcmp(&result,&initial,sizeof(result)) && allocation_calls==allocations);
}

static void readonly_mapping(void)
{
    long queried=sysconf(_SC_PAGESIZE);
    assert(queried>0 && queried<=65536);
    size_t page=(size_t)queried, size=page*2;
    uint8_t *mapping=mmap(NULL,page*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    uint8_t *reference=image_test_malloc(size); /* Fixture allocation, excluded by per-span deltas below. */
    assert(mapping!=MAP_FAILED && reference);
    uint8_t *bytes=mapping+page;
    for (size_t i=0;i<size;++i) reference[i]=bytes[i]=(uint8_t)((i*37+11)&255);
    assert(mprotect(mapping,page,PROT_NONE)==0 && mprotect(bytes,size,PROT_READ)==0 &&
        mprotect(bytes+size,page,PROT_NONE)==0);
#if defined(NATIVE_IMAGE_ASAN)
    /* Deliberately poison valid foreign mapped bytes, like the ASan gaps in
     * readonly PT_LOAD segments. No unpoisoning is needed or performed. */
    __asan_poison_memory_region(bytes+16,size-32);
    assert(__asan_address_is_poisoned(bytes+32));
#endif
    const size_t lengths[]={0,1,55,56,63,64,65,127,128,129,4097,size};
    image_hash seed={.owner=(uintptr_t)mapping,.digest={1,2,3,4},.bytes=7,.spans=2};
    for (size_t i=0;i<sizeof(lengths)/sizeof(lengths[0]);++i) {
        assert(lengths[i]<=size);
        image_hash expected=seed, actual=seed;
        assert(span_bytes(&expected,(uintptr_t)mapping,(uintptr_t)bytes,reference,lengths[i]));
        size_t allocations=allocation_calls;
        assert(span(&actual,(uintptr_t)mapping,(uintptr_t)bytes,lengths[i]));
        assert(!memcmp(&actual,&expected,sizeof(actual)));
#if defined(NATIVE_IMAGE_ASAN)
        assert(allocation_calls==allocations+(lengths[i] ? 1u : 0u));
        if (lengths[i]) assert(allocation_size==lengths[i]);
#else
        assert(allocation_calls==allocations);
#endif
    }
    unchanged_rejection(seed,(uintptr_t)bytes+1,(uintptr_t)bytes,1);
    unchanged_rejection(seed,0,0,1);
    unchanged_rejection(seed,0,UINTPTR_MAX-3,4);
    unchanged_rejection(seed,0,(uintptr_t)bytes,(size_t)IMAGE_LIMIT+1);
    image_hash exhausted=seed;
    exhausted.bytes=IMAGE_LIMIT;
    unchanged_rejection(exhausted,0,(uintptr_t)bytes,1);
    exhausted.bytes=(size_t)IMAGE_LIMIT+1;
    unchanged_rejection(exhausted,0,(uintptr_t)bytes,1);
    exhausted=seed;
    exhausted.spans=SIZE_MAX;
    unchanged_rejection(exhausted,0,(uintptr_t)bytes,1);
#if defined(NATIVE_IMAGE_ASAN)
    reject_allocation=true;
    image_hash failed=seed;
    assert(!span(&failed,(uintptr_t)mapping,(uintptr_t)bytes,size));
    assert(!memcmp(&failed,&seed,sizeof(seed)) && allocation_size==size);
    assert(__asan_address_is_poisoned(bytes+32));
    reject_allocation=false;
#endif
    free(reference);
    assert(munmap(mapping,page*4)==0);
}

#if defined(__linux__)
static void elf_note_controls(void)
{
    uint8_t notes[32]={0};
    ElfW(Nhdr) header={.n_namesz=4,.n_descsz=16,.n_type=NT_GNU_BUILD_ID};
    memcpy(notes,&header,sizeof(header));
    memcpy(notes+sizeof(header),"GNU",4);
    for (size_t i=16;i<sizeof(notes);++i) notes[i]=(uint8_t)i;
    ElfW(Phdr) entries[2]={
        {.p_type=PT_LOAD,.p_flags=PF_R,.p_vaddr=(uintptr_t)notes,.p_memsz=sizeof(notes)},
        {.p_type=PT_NOTE,.p_vaddr=(uintptr_t)notes,.p_memsz=sizeof(notes)}};
    struct dl_phdr_info info={.dlpi_addr=0,.dlpi_phdr=entries,.dlpi_phnum=2};
    image_hash actual={0}, expected={0};
    assert(elf_build_id(&info,&actual));
    assert(span_bytes(&expected,0,(uintptr_t)(notes+16),notes+16,16));
    assert(!memcmp(&actual,&expected,sizeof(actual)));
    const image_hash empty={0};
    size_t allocations=allocation_calls;
    entries[0].p_flags=PF_R|PF_W;
    actual=empty;
    assert(!elf_build_id(&info,&actual) && !memcmp(&actual,&empty,sizeof(actual)));
    entries[0].p_flags=0;
    actual=empty;
    assert(!elf_build_id(&info,&actual) && !memcmp(&actual,&empty,sizeof(actual)));
    entries[0].p_flags=PF_R;
    entries[0].p_memsz=sizeof(notes)-1;
    actual=empty;
    assert(!elf_build_id(&info,&actual) && !memcmp(&actual,&empty,sizeof(actual)));
    entries[0].p_memsz=sizeof(notes);
    entries[1].p_vaddr=UINTPTR_MAX-7;
    entries[1].p_memsz=16;
    actual=empty;
    assert(!elf_build_id(&info,&actual) && !memcmp(&actual,&empty,sizeof(actual)));
    entries[1].p_vaddr=(uintptr_t)notes;
    entries[1].p_memsz=sizeof(notes);
    entries[0].p_memsz=UINTPTR_MAX;
    actual=empty;
    assert(!elf_build_id(&info,&actual) && !memcmp(&actual,&empty,sizeof(actual)));
    assert(allocation_calls==allocations); /* Every rejected extent precedes copying. */
    entries[0].p_memsz=sizeof(notes);
    header.n_descsz=UINT32_MAX;
    memcpy(notes,&header,sizeof(header));
    actual=empty;
    assert(!elf_build_id(&info,&actual) && !memcmp(&actual,&empty,sizeof(actual)));
    assert(allocation_calls==allocations);
}
#endif

int main(void)
{
    readonly_mapping();
#if defined(__linux__)
    elf_note_controls();
#endif
    uint8_t first[32], repeated[32];
    size_t allocations=allocation_calls;
    assert(native_shader_cache_identity(first) && native_shader_cache_identity(repeated));
    assert(!memcmp(first,repeated,sizeof(first)));
#if defined(NATIVE_IMAGE_ASAN)
    assert(allocation_calls>allocations);
    uint8_t output[32], sentinel[32];
    memset(output,0xa5,sizeof(output));
    memcpy(sentinel,output,sizeof(output));
    for (size_t failure=1;failure<=3;++failure) {
        reject_at=allocation_calls+failure;
        assert(!native_shader_cache_identity(output) && !memcmp(output,sentinel,sizeof(output)));
        reject_at=0;
    }
    reject_allocation=true;
#if defined(__linux__)
    assert(!native_build_identity(output) && !memcmp(output,sentinel,sizeof(output)));
#endif
    reject_allocation=false;
    puts("PASS instrumented loaded-image identity: exact span digest/metadata, valid readonly poisoned bytes, guard pages, unchanged poison, invalid extents/notes and OOM rollback; SHA remains instrumented");
#else
    assert(allocation_calls==allocations);
    puts("PASS loaded-image identity: exact span digest/metadata, readonly mapping/guards, invalid extents/notes, repeated real image identity; normal path performs no allocations");
#endif
}
