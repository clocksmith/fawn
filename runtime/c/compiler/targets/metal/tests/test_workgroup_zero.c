/* SPDX-License-Identifier: MIT
 * Emit the actual selected initializer for a host operation control and SDK
 * compilation. Host address-space erasure is not Metal/device qualification. */
#include "fixture.h"
static const char source[] =
    "struct Shared { values:array<array<vec2u,17>,3>,"
    " signed_values:array<atomic<i32>,53>, unsigned_values:array<atomic<u32>,2>, scalar:u32 }"
    "var<workgroup> tile:Shared;"
    "@compute @workgroup_size(2,3,4) fn main(){let x=tile.scalar;}";
static void write_file(const char *prefix, const char *suffix, const char *text) {
    char path[2048];
    const int count = snprintf(path, sizeof(path), "%s%s", prefix, suffix);
    assert(count > 0 && (size_t)count < sizeof(path));
    FILE *file = fopen(path, "w"); assert(file != NULL);
    assert(fputs(text, file) >= 0); assert(fclose(file) == 0);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    Fixture *fixture = prepare(source);
    char *text = emit(fixture, 0u);
    const TypeId type = fixture->module->workgroups[0].type;
    char call[128];
    assert(snprintf(call, sizeof(call), "doe_wzero%u(&w0, input4, 24u);", type) > 0);
    has(text, call);
    has(text, "threadgroup_barrier(mem_flags::mem_threadgroup);");
    has(text, "int(0), memory_order_relaxed)");
    has(text, "uint(0), memory_order_relaxed)");
    assert(strstr(text, "if (input4 == 0u)") == NULL);
    write_file(argv[1], ".metal", text);
    char path[2048];
    const int count = snprintf(path, sizeof(path), "%s.cpp", argv[1]);
    assert(count > 0 && (size_t)count < sizeof(path));
    FILE *file = fopen(path, "w"); assert(file != NULL);
    assert(fputs("#include <atomic>\n#include <cassert>\n#include <cstdio>\n"
        "#define threadgroup\nnamespace metal {"
        "using atomic_int=std::atomic<int>; using atomic_uint=std::atomic<unsigned>;"
        "using std::atomic_store_explicit; using std::memory_order_relaxed;"
        "enum class mem_flags { mem_threadgroup }; inline void threadgroup_barrier(mem_flags) {} }\n", file) >= 0);
    assert(fputs(text, file) >= 0);
    assert(fprintf(file,
        "\nint main() { for (uint stride : {1u,24u,64u,0xffffffffu}) {\n"
        " DoeT%u value;\n"
        " for(uint i=0;i<51u;++i)value.m0[i/17u][i%%17u]=uint2(7u);\n"
        " for(auto& x:value.m1)x.store(-9); for(auto& x:value.m2)x.store(9u); value.m3=7u;\n"
        " for(uint lane=0;lane<min(stride,64u);++lane) { doe_wzero%u(&value,lane,stride);\n"
        "  for(uint i=0;i<51u;++i)for(uint c=0;c<2u;++c)assert(value.m0[i/17u][i%%17u][c]==(i%%stride<=lane?0u:7u));\n"
        "  for(uint i=0;i<53u;++i)assert(value.m1[i].load()==(i%%stride<=lane?0:-9));\n"
        "  for(uint i=0;i<2u;++i)assert(value.m2[i].load()==(i%%stride<=lane?0u:9u)); assert(value.m3==0u);\n"
        " } } puts(\"PASS emitted cooperative zero host operations: nested arrays, signed/unsigned atomics, scalar, sparse/large stride; no Metal execution claim\"); }\n",
        type, type) > 0);
    assert(fclose(file) == 0);
    free(text); release(fixture);
    return 0;
}
