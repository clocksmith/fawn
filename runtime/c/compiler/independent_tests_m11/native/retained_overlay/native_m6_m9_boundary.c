/* SPDX-License-Identifier: MIT
 * M6 additions reuse the exact retained native source without copying it. */
int retained_native_m5_main(int argc, char **argv);
#define main retained_native_m5_main
#include "native_m5.c"
#undef main

typedef struct {
    const char *name;
    const char *source;
    uint32_t dimensions[3];
    uint32_t bindings;
    uint32_t structs;
} M6Seed;

static const M6Seed m6_seeds[] = {
    {"zero", "@compute @workgroup_size(1) fn main(){}", {1u,1u,1u}, 0u,0u},
    {"three_dimensions", "@compute @workgroup_size(4,2,3,) fn main(){return;}",
        {4u,2u,3u}, 0u,0u},
    {"five_inputs", "@group(0) @binding(0) var<storage,read_write> d:array<u32>;"
        "@compute @workgroup_size(4,2,2) fn main("
        "@builtin(local_invocation_index) li:u32,"
        "@builtin(num_workgroups) nw:vec3<u32>,"
        "@builtin(workgroup_id) wg:vec3u,"
        "@builtin(global_invocation_id) gi:vec3u,"
        "@builtin(local_invocation_id) lo:vec3u,){"
        "d[gi.x]=li+nw.y+wg.z+lo.y;}", {4u,2u,2u}, 1u,0u},
    {"indirect", "@group(0) @binding(0) var<storage,read> t:array<u32>;"
        "@group(0) @binding(1) var<storage,read_write> d:array<u32>;"
        "@compute @workgroup_size(2,2) fn main(@builtin(global_invocation_id) g:vec3u){"
        "let n=t[t[g.y]]; if(t[n+1u]<t[t[g.x]]){d[t[g.x]*3u+1u]=t[t[n]]+7u;}}",
        {2u,2u,1u}, 2u,0u},
    {"unused_uniform", "struct P{x:u32,y:f32} @group(0) @binding(0) var<uniform> p:P;"
        "@compute @workgroup_size(1,1,1,) fn main(){}", {1u,1u,1u}, 1u,1u},
    {"unused_five", "@compute @workgroup_size(256,) fn main("
        "@builtin(local_invocation_index) a:u32,"
        "@builtin(local_invocation_id) b:vec3u,"
        "@builtin(workgroup_id) c:vec3u,"
        "@builtin(num_workgroups) d:vec3u,"
        "@builtin(global_invocation_id) e:vec3u,){}", {256u,1u,1u}, 0u,0u},
    {"alias_reordered", "@group(1) @binding(2) var<storage,read_write> d:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(local_invocation_index) z:u32,"
        "@builtin(global_invocation_id) p:vec3u){let i=p.x;d[i]=z;}",
        {1u,1u,1u}, 1u,0u}
};
static const char *const m6_invalid[] = {
    "@compute @workgroup_size(0) fn main(){}",
    "@compute @workgroup_size(257) fn main(){}",
    "@compute @workgroup_size(16,16,2) fn main(){}",
    "@compute @workgroup_size(1,1,1,1) fn main(){}",
    "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) a:vec3u,@builtin(global_invocation_id) b:vec3u){}",
    "@compute @workgroup_size(1) fn main(@builtin(local_invocation_id) a:vec3u,@builtin(workgroup_id) a:vec3u){}",
    "@compute @workgroup_size(1) fn main(@builtin(local_invocation_index) a:vec3u){}",
    "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) a:u32){}",
    "@compute @workgroup_size(1) fn main(@builtin(local_invocation_index) a:u32){let x=a.x;}",
    "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) a:vec3u){let x=a;}",
    "@group(0) @binding(0) var<storage,read_write> d:array<u32>;@compute @workgroup_size(1) fn main(){d[1.0]=1u;}",
    "@compute @workgroup_size(1) fn main(){if(1.0<2u){return;}}",
    "@compute @workgroup_size(1) fn main(){if(1u<2u<3u){return;}}"
};

static void m6_atomic_call(void *workspace, const char *source, size_t length,
        size_t capacity, int expected_success) {
    uint32_t words[WORDS+2u]; DoeCompilerResultV2 result;
    fill_words(words, WORDS+2u); memset(&result, 0xa5, sizeof(result));
    DoeCompilerStatus status = compile_v2(workspace, source, length,
        words+1u, capacity, &result);
    CHECK(status >= DOE_COMPILER_OK && status <= DOE_COMPILER_ABI_MISMATCH);
    if (expected_success >= 0) CHECK((status == DOE_COMPILER_OK) == expected_success);
    CHECK(words[0] == SENTINEL);
    unchanged_words(words+1u+capacity, WORDS+1u-capacity);
    if (status == DOE_COMPILER_OK) {
        CHECK(result.word_count > 5u && result.word_count <= capacity);
        unchanged_words(words+1u+result.word_count, WORDS+1u-result.word_count);
    } else {
        CHECK(result.word_count == 0u);
        zero_bytes(&result.reflection, sizeof(result.reflection));
        unchanged_words(words, WORDS+2u);
        diagnostic_check(&result.diagnostic, status, source, length);
    }
}
static void m6_positive(void *workspace, const M6Seed *seed) {
    const size_t length = strlen(seed->source);
    char *source = malloc(length); CHECK(source != NULL);
    memcpy(source, seed->source, length);
    uint32_t first[WORDS], second[WORDS];
    DoeCompilerResultV2 result; DoeCompilerResult legacy;
    CHECK(compile_v2(workspace, source, length, first, WORDS, &result) == DOE_COMPILER_OK);
    CHECK(result.reflection.binding_count == seed->bindings);
    CHECK(result.reflection.struct_count == seed->structs);
    CHECK(memcmp(result.reflection.workgroup_size, seed->dimensions,
        sizeof(seed->dimensions)) == 0);
    size_t count = result.word_count;
    CHECK(count > 5u && count <= WORDS);
    for (size_t i = seed->bindings; i < DOE_COMPILER_MAX_BINDINGS; ++i)
        zero_bytes(&result.reflection.bindings[i], sizeof(result.reflection.bindings[i]));
    for (size_t i = seed->structs; i < DOE_COMPILER_MAX_STRUCTS; ++i)
        zero_bytes(&result.reflection.structs[i], sizeof(result.reflection.structs[i]));
    m6_atomic_call(workspace, source, length, count, 1);
    m6_atomic_call(workspace, source, length, count-1u, 0);
    m6_atomic_call(workspace, source, length, 0u, 0);
    fill_words(second, WORDS);
    DoeCompilerStatus old_status = compile_v1(1u, source, length, second, WORDS,
        &legacy, sizeof(legacy));
    if (seed->structs != 0u) {
        CHECK(old_status != DOE_COMPILER_OK && legacy.word_count == 0u);
        zero_bytes(&legacy.reflection, sizeof(legacy.reflection));
        unchanged_words(second, WORDS);
    } else {
        CHECK(old_status == DOE_COMPILER_OK && legacy.word_count == count);
        CHECK(memcmp(first, second, count*sizeof(uint32_t)) == 0);
    }
    memset(source, '!', length); free(source);
    CHECK(strcmp(result.reflection.entry_point, "main") == 0);
    m6_atomic_call(workspace, seed->source, length, WORDS, 1);
}
static void m6_mutations(void *workspace, const char *seed, uint32_t *state) {
    const size_t length = strlen(seed);
    char *source = malloc(length); CHECK(source != NULL);
    for (unsigned i = 0u; i < 256u; ++i) {
        memcpy(source, seed, length);
        unsigned count = 1u+random_next(state)%8u;
        for (unsigned j = 0u; j < count; ++j)
            source[random_next(state)%length] = (char)random_next(state);
        size_t used = i%4u == 0u ? random_next(state)%length : length;
        size_t capacity = random_next(state)%(WORDS+1u);
        m6_atomic_call(workspace, source, used, capacity, -1);
        m6_atomic_call(workspace, m6_seeds[0].source,
            strlen(m6_seeds[0].source), WORDS, 1);
    }
    free(source);
}
static void *m6_thread(void *unused) {
    (void)unused;
    void *workspace = malloc(requirements.size_bytes); CHECK(workspace != NULL);
    for (unsigned i = 0u; i < 64u; ++i) {
        const M6Seed *seed = &m6_seeds[i%(sizeof(m6_seeds)/sizeof(m6_seeds[0]))];
        m6_positive(workspace, seed);
    }
    free(workspace); return NULL;
}
int main(int argc, char **argv) {
    CHECK(retained_native_m5_main(argc, argv) == 0);
    CHECK(requirements.size_bytes <= 147456u);
    void *workspace = malloc(requirements.size_bytes); CHECK(workspace != NULL);
    uint32_t state = UINT32_C(0x6d06cafe);
    for (size_t i = 0u; i < sizeof(m6_seeds)/sizeof(m6_seeds[0]); ++i) {
        m6_positive(workspace, &m6_seeds[i]);
        m6_mutations(workspace, m6_seeds[i].source, &state);
    }
    for (size_t i = 0u; i < sizeof(m6_invalid)/sizeof(m6_invalid[0]); ++i) {
        m6_atomic_call(workspace, m6_invalid[i], strlen(m6_invalid[i]), WORDS, i == 9u ? 1 : 0);
        m6_mutations(workspace, m6_invalid[i], &state);
    }
    pthread_t threads[4];
    for (size_t i = 0u; i < 4u; ++i) CHECK(pthread_create(&threads[i], NULL, m6_thread, NULL) == 0);
    for (size_t i = 0u; i < 4u; ++i) CHECK(pthread_join(threads[i], NULL) == 0);
    free(workspace);
    CHECK(atomic_load(&allocation_calls) == 0u && atomic_load(&free_calls) == 0u);
    puts("PASS M6 native: 7 original positive/12 original negative/1 exact M10 positive-lift seeds, 5120 bounded mutations, 4x64 concurrent calls");
    return 0;
}
