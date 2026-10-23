/* SPDX-License-Identifier: MIT
 * Independent public M7 C consumer, allocator interposition and guard pages. */
#define _GNU_SOURCE
#include "doe_compiler_job.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static unsigned allocations;
static int inside_library;
#ifdef JOB_WRAP_ALLOCATORS
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void __real_free(void *pointer);
void *__real_aligned_alloc(size_t alignment, size_t size);
int __real_posix_memalign(void **pointer, size_t alignment, size_t size);
void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_realloc(void *pointer, size_t size);
void __wrap_free(void *pointer);
void *__wrap_aligned_alloc(size_t alignment, size_t size);
int __wrap_posix_memalign(void **pointer, size_t alignment, size_t size);
void *__wrap_malloc(size_t size) { allocations += (unsigned)inside_library; return __real_malloc(size); }
void *__wrap_calloc(size_t count, size_t size) { allocations += (unsigned)inside_library; return __real_calloc(count, size); }
void *__wrap_realloc(void *pointer, size_t size) { allocations += (unsigned)inside_library; return __real_realloc(pointer, size); }
void __wrap_free(void *pointer) { allocations += (unsigned)inside_library; __real_free(pointer); }
void *__wrap_aligned_alloc(size_t alignment, size_t size) { allocations += (unsigned)inside_library; return __real_aligned_alloc(alignment, size); }
int __wrap_posix_memalign(void **pointer, size_t alignment, size_t size) { allocations += (unsigned)inside_library; return __real_posix_memalign(pointer, alignment, size); }
#endif

#define CHECK(c, message) do { if (!(c)) { fprintf(stderr, "job native failure: %s at %d\n", message, __LINE__); return 1; } } while (0)
static const char source_text[] = "@group(0) @binding(0) var<storage,read_write> out:array<u32>; @compute @workgroup_size(1) fn main(){var x:u32; for(var i:u32=0u;i<4u;i++){x+=i;}out[0u]=x;}";
static const char legacy_text[] = "@group(0) @binding(0) var<storage,read_write> out:array<u32>; @compute @workgroup_size(1) fn main(){out[0u]=1u;}";

static int all_bytes(const void *pointer, size_t length, unsigned char value) {
    const unsigned char *bytes = pointer;
    for (size_t index = 0; index < length; ++index) if (bytes[index] != value) return 0;
    return 1;
}

int main(void) {
    const DoeCompilerJobCapacities caps = {65536u,8192u,1024u,4096u,65536u,32u,16u};
    DoeCompilerWorkspaceRequirements req = {0u,0u}, legacy_req = {0u,0u};
    DoeCompilerResultV2 result;
    DoeCompilerResult legacy_result;
    uint32_t words[4096], reference[4096];
    inside_library = 1;
    DoeCompilerStatus status = doe_compiler_job_requirements(3u,&caps,sizeof(caps),&req,sizeof(req));
    inside_library = 0;
    CHECK(status == DOE_COMPILER_OK, "job query");
    CHECK(req.alignment_bytes <= _Alignof(max_align_t) && req.alignment_bytes != 0u, "job alignment");
    CHECK(doe_compiler_workspace_requirements(2u,&legacy_req,sizeof(legacy_req)) == DOE_COMPILER_OK, "legacy query");
    CHECK(legacy_req.size_bytes <= 147456u, "legacy workspace budget");
    unsigned char *workspace = malloc(req.size_bytes + 64u);
    CHECK(workspace != NULL, "caller malloc");
    memset(workspace, 0xA5, req.size_bytes + 64u);
    memset(words, 0xA5, sizeof(words));
    inside_library = 1;
    status = doe_compiler_compile_job(3u,&caps,sizeof(caps),workspace,req.size_bytes,
        source_text,sizeof(source_text)-1u,words,4096u,&result,sizeof(result));
    inside_library = 0;
    CHECK(status == DOE_COMPILER_OK && result.word_count > 5u, "job mutable loop compile");
    CHECK(all_bytes(workspace+req.size_bytes,64u,0xA5), "workspace suffix");
    CHECK(all_bytes(words+result.word_count,sizeof(words)-result.word_count*sizeof(uint32_t),0xA5), "output tail");
    const size_t count = result.word_count;
    memcpy(reference,words,count*sizeof(uint32_t));
    for (unsigned iteration=0u;iteration<12u;++iteration) {
        memset(words,0xA5,sizeof(words));
        const char *selected = iteration%2u == 0u ? source_text : "!";
        size_t length = iteration%2u == 0u ? sizeof(source_text)-1u : 1u;
        inside_library = 1;
        status = doe_compiler_compile_job(3u,&caps,sizeof(caps),workspace,req.size_bytes,selected,length,words,4096u,&result,sizeof(result));
        inside_library = 0;
        if (iteration%2u == 0u) {
            CHECK(status == DOE_COMPILER_OK && result.word_count == count, "workspace reuse success");
            CHECK(memcmp(words,reference,count*sizeof(uint32_t)) == 0, "workspace reuse words");
        } else {
            CHECK(status != DOE_COMPILER_OK && result.word_count == 0u, "workspace reuse failure");
            CHECK(all_bytes(words,sizeof(words),0xA5), "failure words untouched");
            CHECK(all_bytes(&result.reflection,sizeof(result.reflection),0u), "failure reflection clear");
        }
    }
    /* Invalid workspace must never be dereferenced, even to initialize it. */
    long page_value = sysconf(_SC_PAGESIZE);
    CHECK(page_value > 0, "page size");
    size_t page = (size_t)page_value;
    void *denied = mmap(NULL,page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(denied != MAP_FAILED, "guard mmap");
    memset(words,0xA5,sizeof(words));
    status = doe_compiler_compile_job(3u,&caps,sizeof(caps),denied,0u,source_text,sizeof(source_text)-1u,words,4096u,&result,sizeof(result));
    CHECK(status == DOE_COMPILER_INPUT_ERROR && result.diagnostic.offset == 0u, "protected short workspace preflight");
    CHECK(all_bytes(words,sizeof(words),0xA5), "protected preflight words");
    DoeCompilerJobCapacities bad = caps;
    bad.expression_depth = 0u;
    status = doe_compiler_compile_job(3u,&bad,sizeof(bad),denied,req.size_bytes,denied,1u,words,4096u,&result,sizeof(result));
    CHECK(status == DOE_COMPILER_INPUT_ERROR && result.diagnostic.offset == 0u, "invalid capacities before source and workspace reads");
    memset(&result,0xA5,sizeof(result));
    status = doe_compiler_compile_job(99u,(const DoeCompilerJobCapacities *)denied,sizeof(caps),denied,req.size_bytes,denied,1u,words,4096u,&result,sizeof(result));
    CHECK(status == DOE_COMPILER_ABI_MISMATCH && all_bytes(&result,sizeof(result),0xA5), "ABI preflight before any dereference");
    CHECK(munmap(denied,page) == 0, "guard unmap");
    /* Exact non-NUL source ends at an inaccessible page boundary. */
    unsigned char *pages = mmap(NULL,2u*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(pages != MAP_FAILED, "source mmap");
    CHECK(mprotect(pages+page,page,PROT_NONE) == 0, "source protection");
    char *source = (char *)pages+page-(sizeof(source_text)-1u);
    memcpy(source,source_text,sizeof(source_text)-1u);
    status = doe_compiler_compile_job(3u,&caps,sizeof(caps),workspace,req.size_bytes,source,sizeof(source_text)-1u,words,4096u,&result,sizeof(result));
    CHECK(status == DOE_COMPILER_OK, "exact non-NUL source span");
    CHECK(munmap(pages,2u*page) == 0, "source unmap");
    DoeCompilerResultV2 saved = result;
    memset(workspace,0xCE,req.size_bytes);
    CHECK(memcmp(&saved,&result,sizeof(result)) == 0, "result owns no workspace storage");
    /* Exercise all old public calls while allocator interposition is active. */
    void *old_workspace = malloc(legacy_req.size_bytes);
    CHECK(old_workspace != NULL, "legacy caller allocation");
    inside_library = 1;
    status = doe_compiler_compile(1u,legacy_text,sizeof(legacy_text)-1u,words,4096u,&legacy_result,sizeof(legacy_result));
    inside_library = 0;
    CHECK(status == DOE_COMPILER_OK, "ABI1 retained source");
    memcpy(reference,words,legacy_result.word_count*sizeof(uint32_t));
    inside_library = 1;
    status = doe_compiler_compile_v2(2u,old_workspace,legacy_req.size_bytes,legacy_text,sizeof(legacy_text)-1u,words,4096u,&result,sizeof(result));
    inside_library = 0;
    CHECK(status == DOE_COMPILER_OK && result.word_count == legacy_result.word_count, "v2 retained source");
    CHECK(memcmp(reference,words,result.word_count*sizeof(uint32_t)) == 0, "old API words parity");
    inside_library = 1;
    status = doe_compiler_compile_job(3u,&caps,sizeof(caps),workspace,req.size_bytes,legacy_text,sizeof(legacy_text)-1u,words,4096u,&result,sizeof(result));
    inside_library = 0;
    CHECK(status == DOE_COMPILER_OK && result.word_count == legacy_result.word_count, "job retained source");
    CHECK(memcmp(reference,words,result.word_count*sizeof(uint32_t)) == 0, "job API words parity");
    free(old_workspace);
    free(workspace);
    CHECK(allocations == 0u, "library allocated/freed heap storage");
    printf("{\"status\":\"PASS\",\"job_workspace\":%zu,\"job_alignment\":%zu,\"legacy_workspace\":%zu,\"sizeof_capacities\":%zu,\"alignof_capacities\":%zu,\"sizeof_result\":%zu,\"sizeof_requirements\":%zu,\"offsetof_expression_depth\":%zu,\"allocator_calls\":%u}\n",
        req.size_bytes,req.alignment_bytes,legacy_req.size_bytes,sizeof(caps),_Alignof(DoeCompilerJobCapacities),sizeof(result),sizeof(req),offsetof(DoeCompilerJobCapacities,expression_depth),allocations);
    return 0;
}
