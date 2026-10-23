/* SPDX-License-Identifier: MIT
 * Independent M7 CLI boundary; derived from retained M5 fault instrumentation, not compiler code. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "doe_compiler_job.h"

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

static unsigned allocations, writes, reads, closes, links, temps;
static unsigned queries, abi1_calls, abi2_calls, faults, live, lstats;
static unsigned job_queries, job_calls, opens, live_descriptors;
static int descriptors[64];
static DoeCompilerJobCapacities job_caps;
int __real_open(const char *path, int flags, ...);
int __wrap_open(const char *path, int flags, ...);
DoeCompilerStatus __real_doe_compiler_job_requirements(uint32_t version, const DoeCompilerJobCapacities *capacities, size_t capacities_size, DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size);
DoeCompilerStatus __wrap_doe_compiler_job_requirements(uint32_t version, const DoeCompilerJobCapacities *capacities, size_t capacities_size, DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size);
DoeCompilerStatus __real_doe_compiler_compile_job(uint32_t version, const DoeCompilerJobCapacities *capacities, size_t capacities_size, void *workspace, size_t workspace_size, const char *source, size_t length, uint32_t *words, size_t capacity, DoeCompilerResultV2 *result, size_t size);
DoeCompilerStatus __wrap_doe_compiler_compile_job(uint32_t version, const DoeCompilerJobCapacities *capacities, size_t capacities_size, void *workspace, size_t workspace_size, const char *source, size_t length, uint32_t *words, size_t capacity, DoeCompilerResultV2 *result, size_t size);
static size_t sizes[64];
static void *pointers[64];
static size_t workspace_bytes;

static int selected(const char *operation, unsigned call) {
    const char *name = getenv("M7_FAULT_OPERATION");
    const char *number = getenv("M7_FAULT_CALL");
    if (name != NULL && number != NULL && strcmp(name, operation) == 0 &&
        call == (unsigned)strtoul(number, NULL, 10)) {
        ++faults;
        return 1;
    }
    return 0;
}

static void remember_descriptor(int fd) {
    if (fd < 0) return;
    for (unsigned i = 0u; i < 64u; ++i) {
        if (descriptors[i] == 0) { descriptors[i] = fd; ++live_descriptors; return; }
    }
    abort();
}
static void forget_descriptor(int fd) {
    for (unsigned i = 0u; i < 64u; ++i) {
        if (descriptors[i] == fd) { descriptors[i] = 0; --live_descriptors; return; }
    }
}
int __wrap_open(const char *path, int flags, ...) {
    mode_t mode = 0u;
    if ((flags & O_CREAT) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, unsigned int);
        va_end(arguments);
    }
    ++opens;
    if (selected("open", opens)) { errno = EACCES; return -1; }
    int fd = __real_open(path, flags, mode);
    remember_descriptor(fd);
    return fd;
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
    forget_descriptor(fd);
    if (fd > 2 && selected("close", ++closes)) { errno = EIO; return -1; }
    return result;
}
int __wrap_link(const char *old_path, const char *new_path) {
    ++links;
    if (selected("link", links)) { errno = EIO; return -1; }
    if (selected("publish_collision", links)) {
        int fd = __real_open(new_path, O_CREAT | O_EXCL | O_WRONLY, 0600);
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
        int fd = __real_open(template_path, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd < 0) abort();
        if (__real_write(fd, "UNOWNED", 7u) != 7) abort();
        if (__real_close(fd) != 0) abort();
        errno = EEXIST;
        return -1;
    }
    int fd = __real_mkstemp(template_path);
    remember_descriptor(fd);
    return fd;
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
DoeCompilerStatus __wrap_doe_compiler_job_requirements(uint32_t version, const DoeCompilerJobCapacities *capacities, size_t capacities_size, DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    ++job_queries;
    if (selected("job_query", job_queries)) return DOE_COMPILER_ABI_MISMATCH;
    job_caps = *capacities;
    DoeCompilerStatus status = __real_doe_compiler_job_requirements(version,capacities,capacities_size,requirements,requirements_size);
    if (status == DOE_COMPILER_OK) workspace_bytes = requirements->size_bytes;
    return status;
}
DoeCompilerStatus __wrap_doe_compiler_compile_job(uint32_t version, const DoeCompilerJobCapacities *capacities, size_t capacities_size, void *workspace, size_t workspace_size, const char *source, size_t length, uint32_t *words, size_t capacity, DoeCompilerResultV2 *result, size_t size) {
    ++job_calls;
    job_caps = *capacities;
    return __real_doe_compiler_compile_job(version,capacities,capacities_size,workspace,workspace_size,source,length,words,capacity,result,size);
}
int __wrap_main(int argc, char **argv) {
    int status = __real_main(argc, argv);
    fprintf(stderr, "M7_TRACE {\"allocations\":%u,\"live\":%u,\"queries\":%u,"
            "\"abi1_calls\":%u,\"abi2_calls\":%u,\"workspace_bytes\":%zu,"
            "\"writes\":%u,\"reads\":%u,\"closes\":%u,\"links\":%u,"
            "\"temps\":%u,\"faults\":%u,\"lstats\":%u,\"sizes\":[",
            allocations, live, queries, abi1_calls, abi2_calls, workspace_bytes,
            writes, reads, closes, links, temps, faults, lstats);
    for (unsigned i = 0; i < allocations && i < 64u; ++i)
        fprintf(stderr, "%s%zu", i == 0u ? "" : ",", sizes[i]);
    fprintf(stderr, "],\"job_queries\":%u,\"job_calls\":%u,\"opens\":%u,\"live_descriptors\":%u,\"caps\":[%zu,%zu,%zu,%zu,%zu,%u,%u]}\n",job_queries,job_calls,opens,live_descriptors,job_caps.source_bytes,job_caps.tokens,job_caps.statements,job_caps.expressions,job_caps.words,job_caps.expression_depth,job_caps.control_depth);
    return status;
}
