/* SPDX-License-Identifier: MIT
 * Independent M5 CLI boundary fault instrumentation, not compiler code. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "doe_compiler_typed.h"

int __real_main(int argc, char **argv);
int __wrap_main(int argc, char **argv);
void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void *__wrap_realloc(void *pointer, size_t size);
void __real_free(void *pointer);
void __wrap_free(void *pointer);
ssize_t __real_write(int fd, const void *buffer, size_t length);
ssize_t __wrap_write(int fd, const void *buffer, size_t length);
ssize_t __real_read(int fd, void *buffer, size_t length);
ssize_t __wrap_read(int fd, void *buffer, size_t length);
int __real_close(int fd);
int __wrap_close(int fd);
int __real_link(const char *old_path, const char *new_path);
int __wrap_link(const char *old_path, const char *new_path);
int __real_lstat(const char *path, struct stat *state);
int __wrap_lstat(const char *path, struct stat *state);
int __real_mkstemp(char *template_path);
int __wrap_mkstemp(char *template_path);
DoeCompilerStatus __real_doe_compiler_workspace_requirements(
    uint32_t version, DoeCompilerWorkspaceRequirements *requirements, size_t size);
DoeCompilerStatus __wrap_doe_compiler_workspace_requirements(
    uint32_t version, DoeCompilerWorkspaceRequirements *requirements, size_t size);
DoeCompilerStatus __real_doe_compiler_compile(
    uint32_t version, const char *source, size_t length, uint32_t *words,
    size_t capacity, DoeCompilerResult *result, size_t size);
DoeCompilerStatus __wrap_doe_compiler_compile(
    uint32_t version, const char *source, size_t length, uint32_t *words,
    size_t capacity, DoeCompilerResult *result, size_t size);
DoeCompilerStatus __real_doe_compiler_compile_v2(
    uint32_t version, void *workspace, size_t workspace_size,
    const char *source, size_t length, uint32_t *words,
    size_t capacity, DoeCompilerResultV2 *result, size_t size);
DoeCompilerStatus __wrap_doe_compiler_compile_v2(
    uint32_t version, void *workspace, size_t workspace_size,
    const char *source, size_t length, uint32_t *words,
    size_t capacity, DoeCompilerResultV2 *result, size_t size);

static unsigned typed_queries, typed_calls;
static unsigned allocations, writes, reads, closes, links, temps;
static unsigned queries, abi1_calls, abi2_calls, faults, live, lstats;
static size_t sizes[64];
static void *pointers[64];
static size_t workspace_bytes;

static int selected(const char *operation, unsigned call) {
    const char *name = getenv("M5_FAULT_OPERATION");
    const char *number = getenv("M5_FAULT_CALL");
    if (name != NULL && number != NULL && strcmp(name, operation) == 0 &&
        call == (unsigned)strtoul(number, NULL, 10)) {
        ++faults;
        return 1;
    }
    return 0;
}

static void forget(void *pointer) {
    if (pointer == NULL) return;
    for (unsigned i = 0; i < 64u; ++i) {
        if (pointers[i] == pointer) { pointers[i] = NULL; --live; return; }
    }
}

static void remember(void *pointer) {
    if (pointer == NULL) return;
    for (unsigned i = 0; i < 64u; ++i) {
        if (pointers[i] == NULL) { pointers[i] = pointer; ++live; return; }
    }
    abort();
}

static int allocate_fault(size_t size) {
    if (allocations < 64u) sizes[allocations] = size;
    ++allocations;
    if (selected("allocation", allocations)) { errno = ENOMEM; return 1; }
    return 0;
}

void *__wrap_malloc(size_t size) {
    if (allocate_fault(size)) return NULL;
    void *pointer = __real_malloc(size); remember(pointer); return pointer;
}
void *__wrap_calloc(size_t count, size_t size) {
    if (count != 0u && size > SIZE_MAX / count) { errno = ENOMEM; return NULL; }
    if (allocate_fault(count * size)) return NULL;
    void *pointer = __real_calloc(count, size); remember(pointer); return pointer;
}
void *__wrap_realloc(void *pointer, size_t size) {
    if (allocate_fault(size)) return NULL;
    /* Remove before realloc to avoid retaining or examining a freed value. */
    forget(pointer);
    void *replacement = __real_realloc(pointer, size);
    if (replacement != NULL) remember(replacement);
    else if (size != 0u) remember(pointer);
    return replacement;
}
void __wrap_free(void *pointer) { forget(pointer); __real_free(pointer); }

ssize_t __wrap_write(int fd, const void *buffer, size_t length) {
    if (fd > 2) {
        ++writes;
        if (selected("write", writes)) { errno = ENOSPC; return -1; }
        if (selected("write_eintr", writes)) { errno = EINTR; return -1; }
        if (selected("write_short", writes) && length > 1u)
            return __real_write(fd, buffer, length / 2u);
    }
    return __real_write(fd, buffer, length);
}
ssize_t __wrap_read(int fd, void *buffer, size_t length) {
    if (fd > 2) {
        ++reads;
        if (selected("read", reads)) { errno = EIO; return -1; }
        if (selected("read_eintr", reads)) { errno = EINTR; return -1; }
        if (selected("read_short", reads) && length > 1u)
            return __real_read(fd, buffer, length / 2u);
    }
    return __real_read(fd, buffer, length);
}
int __wrap_close(int fd) {
    int result = __real_close(fd);
    if (fd > 2 && selected("close", ++closes)) { errno = EIO; return -1; }
    return result;
}
int __wrap_link(const char *old_path, const char *new_path) {
    ++links;
    if (selected("link", links)) { errno = EIO; return -1; }
    if (selected("publish_collision", links)) {
        int fd = open(new_path, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd < 0) abort();
        if (__real_write(fd, "UNOWNED", 7u) != 7) abort();
        if (__real_close(fd) != 0) abort();
    }
    return __real_link(old_path, new_path);
}
int __wrap_lstat(const char *path, struct stat *state) {
    ++lstats;
    return __real_lstat(path, state);
}
int __wrap_mkstemp(char *template_path) {
    ++temps;
    if (selected("mkstemp", temps)) { errno = EACCES; return -1; }
    if (selected("unowned_temp", temps)) {
        size_t length = strlen(template_path);
        if (length < 6u) abort();
        memcpy(template_path + length - 6u, "EVAL00", 6u);
        int fd = open(template_path, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd < 0) abort();
        if (__real_write(fd, "UNOWNED", 7u) != 7) abort();
        if (__real_close(fd) != 0) abort();
        errno = EEXIST;
        return -1;
    }
    return __real_mkstemp(template_path);
}
DoeCompilerStatus __wrap_doe_compiler_workspace_requirements(
    uint32_t version, DoeCompilerWorkspaceRequirements *requirements, size_t size) {
    if (selected("query", ++queries)) return DOE_COMPILER_ABI_MISMATCH;
    DoeCompilerStatus status = __real_doe_compiler_workspace_requirements(
        version, requirements, size);
    if (status == DOE_COMPILER_OK) workspace_bytes = requirements->size_bytes;
    return status;
}
DoeCompilerStatus __wrap_doe_compiler_compile(
    uint32_t version, const char *source, size_t length, uint32_t *words,
    size_t capacity, DoeCompilerResult *result, size_t size) {
    ++abi1_calls;
    return __real_doe_compiler_compile(version, source, length, words,
                                       capacity, result, size);
}
DoeCompilerStatus __wrap_doe_compiler_compile_v2(
    uint32_t version, void *workspace, size_t workspace_size,
    const char *source, size_t length, uint32_t *words,
    size_t capacity, DoeCompilerResultV2 *result, size_t size) {
    ++abi2_calls;
    return __real_doe_compiler_compile_v2(version, workspace, workspace_size,
                                         source, length, words, capacity,
                                         result, size);
}
int __wrap_main(int argc, char **argv) {
    int status = __real_main(argc, argv);
    fprintf(stderr, "M10_TYPED {\"queries\":%u,\"calls\":%u}\n", typed_queries, typed_calls);
    fprintf(stderr, "M5_TRACE {\"allocations\":%u,\"live\":%u,\"queries\":%u,"
            "\"abi1_calls\":%u,\"abi2_calls\":%u,\"workspace_bytes\":%zu,"
            "\"writes\":%u,\"reads\":%u,\"closes\":%u,\"links\":%u,"
            "\"temps\":%u,\"faults\":%u,\"lstats\":%u,\"sizes\":[",
            allocations, live, queries, abi1_calls, abi2_calls, workspace_bytes,
            writes, reads, closes, links, temps, faults, lstats);
    for (unsigned i = 0; i < allocations && i < 64u; ++i)
        fprintf(stderr, "%s%zu", i == 0u ? "" : ",", sizes[i]);
    fputs("]}\n", stderr);
    return status;
}

/* New ABI4 wrappers; the I/O/allocator instrumentation above is inherited from
 * the immutable accepted M5 evaluator at dee5f42, not compiler implementation. */
DoeCompilerStatus __real_doe_compiler_typed_requirements(uint32_t, const DoeCompilerTypedCapacities *, size_t, DoeCompilerWorkspaceRequirements *, size_t);
DoeCompilerStatus __wrap_doe_compiler_typed_requirements(uint32_t, const DoeCompilerTypedCapacities *, size_t, DoeCompilerWorkspaceRequirements *, size_t);
DoeCompilerStatus __real_doe_compiler_compile_typed(uint32_t, const DoeCompilerTypedCapacities *, size_t, void *, size_t, const char *, size_t, uint32_t, const DoeCompilerTypedOutputs *, size_t, DoeCompilerTypedResult *, size_t);
DoeCompilerStatus __wrap_doe_compiler_compile_typed(uint32_t, const DoeCompilerTypedCapacities *, size_t, void *, size_t, const char *, size_t, uint32_t, const DoeCompilerTypedOutputs *, size_t, DoeCompilerTypedResult *, size_t);
DoeCompilerStatus __wrap_doe_compiler_typed_requirements(uint32_t v, const DoeCompilerTypedCapacities *c, size_t cs, DoeCompilerWorkspaceRequirements *r, size_t rs) {
    if (selected("typed_query", ++typed_queries)) return DOE_COMPILER_INPUT_ERROR;
    DoeCompilerStatus status = __real_doe_compiler_typed_requirements(v,c,cs,r,rs);
    if (status == DOE_COMPILER_OK) workspace_bytes = r->size_bytes;
    return status;
}
DoeCompilerStatus __wrap_doe_compiler_compile_typed(uint32_t v, const DoeCompilerTypedCapacities *c, size_t cs, void *w, size_t ws, const char *s, size_t n, uint32_t flags, const DoeCompilerTypedOutputs *o, size_t os, DoeCompilerTypedResult *r, size_t rs) {
    if (selected("typed_compile", ++typed_calls)) {
        if (r != NULL && rs == sizeof(*r)) {
            memset(r,0,sizeof(*r));r->diagnostic.status=DOE_COMPILER_INPUT_ERROR;
        }
        return DOE_COMPILER_INPUT_ERROR;
    }
    return __real_doe_compiler_compile_typed(v,c,cs,w,ws,s,n,flags,o,os,r,rs);
}
