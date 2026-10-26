/* SPDX-License-Identifier: MIT */
#include "../src/shader_cache.h"
#include "../src/shader_disk_cache.h"
#include "../src/shader_cache_identity.h"
#include "../../shader/src/artifact_internal.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct allocator { size_t live; bool fail; unsigned char *mutate; doe_shader *release; } allocator;
static void *allocate(void *user, size_t bytes, size_t alignment)
{
    allocator *state = user;
    assert(alignment <= alignof(max_align_t));
    if (state->mutate) { *state->mutate ^= 1; state->mutate = NULL; }
    if (state->release) { doe_shader *shader = state->release; state->release = NULL; doe_shader_release(shader); }
    if (state->fail) return NULL;
    void *memory = malloc(bytes);
    if (memory) ++state->live;
    return memory;
}
static void deallocate(void *user, void *memory, size_t bytes, size_t alignment)
{
    allocator *state = user;
    assert(state->live && bytes && alignment <= alignof(max_align_t));
    --state->live;
    free(memory);
}
static doe_shader_options options(allocator *state)
{
    return (doe_shader_options){.target=DOE_SHADER_METAL,.allocator={state,allocate,deallocate},
        .capacities={.typed={.source_bytes=8192,.tokens=1024,.statements=256,.expressions=1024,
            .types=64,.members=128,.resources=16,.access_steps=1024,.constants=256,.constant_edges=512,
            .ir_instructions=4096,.ir_operands=2048,.target_declarations=4096,.traversal_items=193,
            .words=32768,.expression_depth=32,.control_depth=16,.type_syntax_depth=32},
            .entries=8,.entry_inputs=64,.entry_references=128},.interface_attributes=64,.stage_interfaces=64,.sampling_pairs=128,
        .compiler_identity="cache-test",.compiler_identity_size=10,
        .semantic_flags=DOE_COMPILER_CORE_BINDING_NUMBERS};
}
static void cache_capacity_controls(allocator *state)
{
    native_shader_cache cache = {0};
    doe_shader_options policy = options(state);
    doe_shader *empty = NULL;
    doe_shader_error error;
    assert(doe_shader_create_module(&policy,8,NULL,0,&empty,&error) == DOE_SHADER_OK);
    assert(!native_shader_cache_insert(&cache,empty));
    doe_shader *hit = native_shader_cache_module(&cache,&policy,NULL,0);
    assert(hit == empty); doe_shader_release(hit);
    for (size_t i = 0; i < NATIVE_SHADER_CACHE_SLOTS; ++i) {
        char source[96];
        int size = snprintf(source,sizeof(source),"@compute @workgroup_size(1) fn entry_%zu(){}",i);
        assert(size > 0 && (size_t)size < sizeof(source));
        doe_shader *module = NULL;
        assert(doe_shader_create_module(&policy,8,source,(size_t)size,&module,&error) == DOE_SHADER_OK);
        doe_shader_release(native_shader_cache_insert(&cache,module));
        doe_shader_release(module);
    }
    assert(!native_shader_cache_module(&cache,&policy,NULL,0));
    doe_shader_release(empty); native_shader_cache_clear(&cache);
    size_t size = 1024u * 1024u;
    char *source = malloc(size); assert(source); memset(source,' ',size);
    policy.capacities.typed.source_bytes = size;
    doe_shader *oversized = NULL;
    assert(doe_shader_create_module(&policy,8,source,size,&oversized,&error) == DOE_SHADER_OK);
    assert(!native_shader_cache_admits(oversized));
    assert(!native_shader_cache_insert(&cache,oversized));
    assert(!native_shader_cache_module(&cache,&policy,source,size));
    doe_shader_release(oversized); native_shader_cache_clear(&cache); free(source);
    assert(state->live == 0);
}
static void disk_roundtrip(allocator *state)
{
    const char source[] = "struct Data { value:u32, }; @group(0) @binding(0) var<storage,read_write> output:array<Data>;"
        "override width:u32=1; @compute @workgroup_size(width) fn chosen(){output[0].value=width;}";
    const DoeCompilerEntrySelectionV5 selection = {.mode=DOE_COMPILER_SELECT_NAME,.name="chosen",.name_length=6};
    DoeCompilerOverrideValueV13 value = {.key="width",.key_length=5,.value=2};
    const char *directory = getenv("DOE_SHADER_CACHE_DIR");
    assert(directory && *directory);
    for (unsigned target = 0; target < 3; ++target) {
        doe_shader_options policy = options(state); policy.target = (doe_shader_target)target;
        doe_shader *module = NULL, *stage = NULL, *loaded = NULL;
        doe_shader_error error;
        assert(doe_shader_create_module(&policy,8,source,sizeof(source)-1,&module,&error) == DOE_SHADER_OK);
        assert(doe_shader_specialize(module,&selection,&value,1,&stage,&error) == DOE_SHADER_OK);
        uint8_t image[32] = {1}, key[32], combined[64];
        native_shader_disk_store(image,module,&selection,&value,1,NULL,stage);
        loaded = native_shader_disk_lookup(image,module,&selection,&value,1,NULL);
        assert(loaded && doe_shader_specialization_matches(loaded,module,&selection,&value,1));
        doe_shader_view original, restored;
        assert(doe_shader_borrow(stage,&original) == DOE_SHADER_OK);
        assert(doe_shader_borrow(loaded,&restored) == DOE_SHADER_OK);
#define COMPARE(field,type,count) assert(original.count == restored.count); \
        if (original.count) assert(!memcmp(original.field,restored.field,original.count*sizeof(type)));
        SHADER_ARTIFACT_SPANS(COMPARE)
#undef COMPARE
        doe_shader_release(loaded); loaded = NULL;
        image[0] = 2;
        assert(!native_shader_disk_lookup(image,module,&selection,&value,1,NULL));
        image[0] = 1; value.value = 3;
        assert(!native_shader_disk_lookup(image,module,&selection,&value,1,NULL));
        value.value = 2;
        state->fail = true;
        assert(!native_shader_disk_lookup(image,module,&selection,&value,1,NULL));
        state->fail = false;
        assert(doe_shader_artifact_key(module,&selection,&value,1,NULL,key));
        memcpy(combined,image,32); memcpy(combined+32,key,32);
        doe_common_sha256(combined,sizeof(combined),key);
        char hex[65], path[4096]; doe_common_sha256_hex(key,hex);
        int written = snprintf(path,sizeof(path),"%s/%s.bin",directory,hex);
        assert(written > 0 && (size_t)written < sizeof(path));
        FILE *file = fopen(path,"wb"); assert(file);
        assert(fwrite("corrupted",1,9,file) == 9 && fclose(file) == 0);
        assert(!native_shader_disk_lookup(image,module,&selection,&value,1,NULL));
        native_shader_disk_store(image,module,&selection,&value,1,NULL,stage);
        loaded = native_shader_disk_lookup(image,module,&selection,&value,1,NULL);
        assert(loaded);
        doe_shader_release(loaded); doe_shader_release(stage); doe_shader_release(module);
        assert(state->live == 0);
    }
}
static void artifact_graph_controls(allocator *state)
{
    doe_shader_options policy = options(state);
    const char source[] = "struct S { x:u32, }; @group(0) @binding(0) var<storage,read_write> out:S;"
        "@compute @workgroup_size(1) fn main(){out.x=7;}";
    const DoeCompilerEntrySelectionV5 selection = {.mode=DOE_COMPILER_SELECT_AUTO};
    doe_shader *module = NULL, *stage = NULL, *imported = NULL;
    doe_shader_error error;
    assert(doe_shader_create_module(&policy,8,source,sizeof(source)-1,&module,&error) == DOE_SHADER_OK);
    assert(doe_shader_specialize(module,&selection,NULL,0,&stage,&error) == DOE_SHADER_OK);
    size_t size = doe_shader_artifact_size(stage);
    void *bytes = malloc(size); assert(bytes && doe_shader_artifact_write(stage,bytes,size));
    shader_artifact_header *header = bytes;
    size_t cursor = sizeof(*header), offsets[SHADER_ARTIFACT_SPAN_COUNT], index = 0;
#define OFFSET(field,type,count) assert(shader_reserve(&cursor,(size_t)header->counts[index],sizeof(type),alignof(type),&offsets[index])); ++index;
    SHADER_ARTIFACT_SPANS(OFFSET)
#undef OFFSET
    assert(cursor == size);
    DoeCompilerTypeV4 *types = (DoeCompilerTypeV4 *)((unsigned char *)bytes+offsets[1]);
    uint32_t saved = types[0].element_type;
    types[0].element_type = 0;
    doe_common_sha256((unsigned char *)bytes+32,size-32,header->digest);
    assert(doe_shader_artifact_read(module,&selection,NULL,0,NULL,bytes,size,&imported) != DOE_SHADER_OK && !imported);
    types[0].element_type = saved;
    DoeCompilerEntryV5 *entries = (DoeCompilerEntryV5 *)((unsigned char *)bytes+offsets[5]);
    saved = entries[0].first_resource_reference;
    entries[0].first_resource_reference = UINT32_MAX;
    doe_common_sha256((unsigned char *)bytes+32,size-32,header->digest);
    assert(doe_shader_artifact_read(module,&selection,NULL,0,NULL,bytes,size,&imported) != DOE_SHADER_OK && !imported);
    entries[0].first_resource_reference = saved;
    doe_common_sha256((unsigned char *)bytes+32,size-32,header->digest);
    doe_shader_release(stage);
    state->release = module; /* Callback consumes its one independent owner. */
    assert(doe_shader_artifact_read(module,&selection,NULL,0,NULL,bytes,size,&imported) == DOE_SHADER_OK);
    doe_shader_release(imported); free(bytes);
    assert(state->live == 0 && !state->release);
}
#ifndef DOE_CACHE_IDENTITY_VARIANT
#define DOE_CACHE_IDENTITY_VARIANT 0
#endif
static int process_cache(const char *mode)
{
    uint8_t image[32], repeated[32];
    if (!strcmp(mode,"identity-absent")) {
        memset(image,0xa5,sizeof(image));
        assert(!native_shader_cache_identity(image));
        for (size_t i = 0; i < sizeof(image); ++i) assert(image[i] == 0xa5);
        puts("native disk cache: missing loaded-image build ID fails closed");
        return 0;
    }
    assert(native_shader_cache_identity(image) && native_shader_cache_identity(repeated));
    assert(!memcmp(image,repeated,32));
    allocator state = {0};
    doe_shader_options policy = options(&state);
    const char source[] = "@compute @workgroup_size(1) fn chosen(){}";
    const DoeCompilerEntrySelectionV5 selection = {.mode=DOE_COMPILER_SELECT_AUTO};
    doe_shader *module = NULL, *stage = NULL;
    doe_shader_error error;
    assert(doe_shader_create_module(&policy,8,source,sizeof(source)-1,&module,&error) == DOE_SHADER_OK);
    if (!strcmp(mode,"warm")) {
        assert(doe_shader_specialize(module,&selection,NULL,0,&stage,&error) == DOE_SHADER_OK);
        native_shader_disk_store(image,module,&selection,NULL,0,NULL,stage);
    } else {
        assert(!strcmp(mode,"reload") || !strcmp(mode,"reload-miss"));
        /* No target compiler call occurs on this process path. */
        stage = native_shader_disk_lookup(image,module,&selection,NULL,0,NULL);
        if (!strcmp(mode,"reload-miss")) assert(!stage && DOE_CACHE_IDENTITY_VARIANT);
        else assert(stage && doe_shader_specialization_matches(stage,module,&selection,NULL,0));
    }
    doe_shader_release(stage); doe_shader_release(module);
    assert(state.live == 0);
    puts(!strcmp(mode,"warm") ? "native disk cache: real loaded-image artifact written" :
        DOE_CACHE_IDENTITY_VARIANT ? "native disk cache: changed actual consumer image misses" :
        "native disk cache: fresh-process target hit without target compilation");
    return 0;
}
int main(int argc, char **argv)
{
    if (argc == 2) return process_cache(argv[1]);
    allocator state = {0};
    native_shader_cache first = {0}, second = {0};
    doe_shader_options policy = options(&state);
    char source[] = "override width:u32=1; @compute @workgroup_size(width) fn chosen(){} @compute @workgroup_size(1) fn other(){}";
    doe_shader *module = NULL, *stage = NULL;
    doe_shader_error error;
    assert(doe_shader_create_module(&policy,8,source,strlen(source),&module,&error) == DOE_SHADER_OK);
    assert(native_shader_cache_insert(&first,module) == NULL);
    assert(native_shader_cache_insert(&first,module) == NULL);
    doe_shader *hit = native_shader_cache_module(&first,&policy,source,strlen(source));
    assert(hit == module); doe_shader_release(hit);
    assert(!native_shader_cache_module(&second,&policy,source,strlen(source)));
    source[0] = 'O';
    assert(!native_shader_cache_module(&first,&policy,source,strlen(source)));
    source[0] = 'o';
    policy.semantic_flags ^= DOE_COMPILER_DISABLE_F16;
    assert(!native_shader_cache_module(&first,&policy,source,strlen(source)));
    policy = options(&state); policy.target = DOE_SHADER_SPIRV;
    assert(!native_shader_cache_module(&first,&policy,source,strlen(source)));
    policy = options(&state); policy.compiler_identity = "cache-TEST";
    assert(!native_shader_cache_module(&first,&policy,source,strlen(source)));
    policy = options(&state);
    DoeCompilerEntrySelectionV5 selection = {.mode=DOE_COMPILER_SELECT_NAME,.name="chosen",.name_length=6};
    DoeCompilerOverrideValueV13 value = {.key="width",.key_length=5,.value=2};
    assert(doe_shader_specialize(module,&selection,&value,1,&stage,&error) == DOE_SHADER_OK);
    size_t serialized_size = doe_shader_artifact_size(stage);
    assert(serialized_size);
    void *serialized = malloc(serialized_size);
    assert(serialized && doe_shader_artifact_write(stage,serialized,serialized_size));
    doe_shader *imported = NULL;
    assert(doe_shader_artifact_read(module,&selection,&value,1,NULL,serialized,serialized_size,&imported) == DOE_SHADER_OK);
    assert(doe_shader_specialization_matches(imported,module,&selection,&value,1));
    doe_shader_view original_view, imported_view;
    assert(doe_shader_borrow(stage,&original_view) == DOE_SHADER_OK);
    assert(doe_shader_borrow(imported,&imported_view) == DOE_SHADER_OK);
    assert(imported_view.text_size == original_view.text_size &&
        !memcmp(imported_view.text,original_view.text,original_view.text_size));
    doe_shader_release(imported); imported = NULL;
    ((unsigned char *)serialized)[serialized_size-1] ^= 1;
    assert(doe_shader_artifact_read(module,&selection,&value,1,NULL,serialized,serialized_size,&imported) != DOE_SHADER_OK && !imported);
    ((unsigned char *)serialized)[serialized_size-1] ^= 1;
    state.mutate = (unsigned char *)serialized+serialized_size-1;
    assert(doe_shader_artifact_read(module,&selection,&value,1,NULL,serialized,serialized_size,&imported) != DOE_SHADER_OK && !imported);
    ((unsigned char *)serialized)[serialized_size-1] ^= 1;
    shader_artifact_header *header = serialized;
    header->counts[0] = UINT64_MAX;
    doe_common_sha256((unsigned char *)serialized+32,serialized_size-32,header->digest);
    assert(doe_shader_artifact_read(module,&selection,&value,1,NULL,serialized,serialized_size,&imported) != DOE_SHADER_OK && !imported);
    free(serialized);
    assert(!native_shader_cache_insert(&first,stage));
    hit = native_shader_cache_stage(&first,module,&selection,&value,1,NULL);
    assert(hit == stage); doe_shader_release(hit);
    assert(!native_shader_cache_stage(&second,module,&selection,&value,1,NULL));
    value.value = 3;
    assert(!native_shader_cache_stage(&first,module,&selection,&value,1,NULL));
    value.value = 2;
    DoeCompilerMetalPipelineOptionsV15 mask = {.sample_mask=0};
    assert(!native_shader_cache_stage(&first,module,&selection,&value,1,&mask));
    selection.name = "other"; selection.name_length = 5;
    assert(!native_shader_cache_stage(&first,module,&selection,&value,1,NULL));
    state.fail = true;
    doe_shader *failed = NULL;
    assert(doe_shader_create_module(&policy,8,source,strlen(source),&failed,&error) == DOE_SHADER_OUT_OF_MEMORY);
    assert(!failed && !native_shader_cache_insert(&first,failed));
    hit = native_shader_cache_module(&first,&policy,source,strlen(source));
    assert(hit == module); doe_shader_release(hit);
    state.fail = false;
    assert(doe_shader_create_module(&policy,8,"invalid",7,&failed,&error) != DOE_SHADER_OK);
    assert(!failed);
    doe_shader_release(module); doe_shader_release(stage);
    native_shader_cache_clear(&first); native_shader_cache_clear(&second);
    assert(state.live == 0);
    assert(doe_shader_owned_bytes(NULL) == 0);
    assert(!doe_shader_module_matches(NULL,DOE_SHADER_METAL,0,NULL,0,NULL,0));
    cache_capacity_controls(&state);
    artifact_graph_controls(&state);
    disk_roundtrip(&state);
    puts("native shader cache: persisted all-target reflection/bytes, corrupt/OOM/build misses; exact module/stage policy, isolation, failed/OOM and lifetime passed");
    return 0;
}
