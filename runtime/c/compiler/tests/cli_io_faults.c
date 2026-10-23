/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Test-only linker wrappers; never linked into the shipped CLI or library. */
#define _POSIX_C_SOURCE 200809L
#include "../include/doe_compiler_job.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ssize_t __real_write(int fd, const void *bytes, size_t length);
int __real_close(int fd);
int __real_link(const char *old_path, const char *new_path);
int __real_mkstemp(char *path);
ssize_t __wrap_write(int fd, const void *bytes, size_t length);
int __wrap_close(int fd);
int __wrap_link(const char *old_path, const char *new_path);
int __wrap_mkstemp(char *path);

static int selected(const char *operation, unsigned call) {
    const char *name = getenv("DOE_CLI_FAULT");
    const char *number = getenv("DOE_CLI_FAULT_CALL");
    return name != NULL && number != NULL && strcmp(name, operation) == 0 &&
           strtoul(number, NULL, 10) == call;
}
ssize_t __wrap_write(int fd, const void *bytes, size_t length) {
    static unsigned calls;
    ++calls;
    if (selected("write", calls) || selected("partial", calls - 1u)) {
        errno = EIO; return -1;
    }
    if (selected("eintr", calls)) { errno = EINTR; return -1; }
    if (selected("partial", calls) && length > 1u) length /= 2u;
    const char *name = getenv("DOE_CLI_FAULT");
    if (name != NULL && strcmp(name, "short") == 0 && length > 17u) length = 17u;
    return __real_write(fd, bytes, length);
}
int __wrap_close(int fd) {
    static unsigned calls;
    ++calls;
    const int status = __real_close(fd);
    if (selected("close", calls)) { errno = EIO; return -1; }
    return status;
}
int __wrap_link(const char *old_path, const char *new_path) {
    static unsigned calls;
    ++calls;
    if (selected("link", calls)) { errno = EIO; return -1; }
    if (selected("race", calls)) {
        const int fd = open(new_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0) return -1;
        static const char foreign[] = "unowned destination";
        if (__real_write(fd, foreign, sizeof(foreign) - 1u) < 0) {
            (void)__real_close(fd); return -1;
        }
        if (__real_close(fd) < 0) return -1;
    }
    if (selected("race_symlink", calls) && symlink("unowned-target", new_path) < 0)
        return -1;
    return __real_link(old_path, new_path);
}
int __wrap_mkstemp(char *path) {
    static unsigned calls;
    ++calls;
    if (selected("temporary", calls)) { errno = ENOSPC; return -1; }
    return __real_mkstemp(path);
}

void *__real_malloc(size_t size);
void __real_free(void *pointer);
void *__wrap_malloc(size_t size);
void __wrap_free(void *pointer);
DoeCompilerStatus __real_doe_compiler_job_requirements(uint32_t version,
    const DoeCompilerJobCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t size);
DoeCompilerStatus __wrap_doe_compiler_job_requirements(uint32_t version,
    const DoeCompilerJobCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t size);
enum { TRACKED_ALLOCATIONS = 5 };
static void *allocations[TRACKED_ALLOCATIONS];
static void allocation_cleanup(void) {
    for (size_t i = 0; i < TRACKED_ALLOCATIONS; ++i) assert(allocations[i] == NULL);
}
void *__wrap_malloc(size_t size) {
    static unsigned calls;
    if (calls++ == 0u) assert(atexit(allocation_cleanup) == 0);
    /* A failed requirements query must never be followed by allocation. */
    assert(!selected("query", 1u));
    if (selected("malloc", calls)) { errno = ENOMEM; return NULL; }
    void *pointer = __real_malloc(size);
    if (pointer != NULL) {
        size_t slot = 0;
        while (slot < TRACKED_ALLOCATIONS && allocations[slot] != NULL) ++slot;
        assert(slot < TRACKED_ALLOCATIONS); allocations[slot] = pointer;
    }
    return pointer;
}
void __wrap_free(void *pointer) {
    if (pointer != NULL) {
        size_t slot = 0;
        while (slot < TRACKED_ALLOCATIONS && allocations[slot] != pointer) ++slot;
        assert(slot < TRACKED_ALLOCATIONS); allocations[slot] = NULL;
    }
    __real_free(pointer);
}
DoeCompilerStatus __wrap_doe_compiler_job_requirements(uint32_t version,
    const DoeCompilerJobCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t size) {
    if (selected("query", 1u)) return DOE_COMPILER_ABI_MISMATCH;
    return __real_doe_compiler_job_requirements(version, capacities, capacities_size, requirements, size);
}
