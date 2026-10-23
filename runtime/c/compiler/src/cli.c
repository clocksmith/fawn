/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _POSIX_C_SOURCE 200809L
#include "../include/doe_compiler.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_CAPACITY 4096u
static int write_all(int fd, const unsigned char *bytes, size_t length) {
    size_t done = 0;
    while (done < length) {
        const ssize_t count = write(fd, bytes + done, length - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        done += (size_t)count;
    }
    return 0;
}
static int temporary(const char *path, char output[PATH_CAPACITY]) {
    const size_t length = strlen(path);
    if (length > PATH_CAPACITY - sizeof(".tmp.XXXXXX")) { errno = ENAMETOOLONG; return -1; }
    memcpy(output, path, length);
    memcpy(output + length, ".tmp.XXXXXX", sizeof(".tmp.XXXXXX"));
    return mkstemp(output);
}
static int unused_destination(const char *path) {
    struct stat state;
    if (lstat(path, &state) == 0) { errno = EEXIST; return -1; }
    return errno == ENOENT ? 0 : -1;
}
int main(int argc, char **argv) {
    const char *input = NULL, *output = NULL, *reflection = NULL;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) { fprintf(stderr, "each option requires a path\n"); return 2; }
        const char **slot = NULL;
        if (strcmp(argv[i], "--shader-path") == 0) slot = &input;
        else if (strcmp(argv[i], "--out") == 0) slot = &output;
        else if (strcmp(argv[i], "--reflection") == 0) slot = &reflection;
        if (slot == NULL || *slot != NULL) { fprintf(stderr, "unknown or duplicate option: %s\n", argv[i]); return 2; }
        *slot = argv[i + 1];
    }
    if (input == NULL || output == NULL || reflection == NULL) {
        fprintf(stderr, "usage: doe-compiler-c --shader-path INPUT --out OUTPUT --reflection JSON\n");
        return 2;
    }
    if (strcmp(output, reflection) == 0) { fprintf(stderr, "output and reflection paths must differ\n"); return 2; }
    int status = 1, input_fd = -1, binary_fd = -1, json_fd = -1;
    int binary_published = 0, json_published = 0;
    int binary_created = 0, json_created = 0;
    char binary_temp[PATH_CAPACITY] = {0}, json_temp[PATH_CAPACITY] = {0};
    char source[DOE_COMPILER_MAX_SOURCE_BYTES + 1u];
    size_t length = 0;
    input_fd = open(input, O_RDONLY);
    if (input_fd < 0) { perror("open shader"); goto cleanup; }
    while (length < sizeof(source)) {
        const ssize_t count = read(input_fd, source + length, sizeof(source) - length);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { perror("read shader"); goto cleanup; }
        if (count == 0) break;
        length += (size_t)count;
    }
    if (close(input_fd) < 0) { input_fd = -1; perror("close shader"); goto cleanup; }
    input_fd = -1;
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult result;
    if (doe_compiler_compile(DOE_COMPILER_ABI_VERSION, source, length,
                             words, DOE_COMPILER_MAX_WORDS, &result, sizeof(result)) != DOE_COMPILER_OK) {
        fprintf(stderr, "%s:%u:%u: category=%u offset=%zu: %s\n", input,
                result.diagnostic.line, result.diagnostic.column, (unsigned)result.diagnostic.status,
                result.diagnostic.offset, result.diagnostic.message);
        goto cleanup;
    }
    if (unused_destination(output) < 0 || unused_destination(reflection) < 0) {
        perror("destination unavailable (existing files are never overwritten)"); goto cleanup;
    }
    const DoeCompilerReflection *r = &result.reflection;
    char json[4096];
    int written = snprintf(json, sizeof(json),
        "{\n  \"entry_point\": \"%s\",\n"
        "  \"workgroup_size\": [%u, %u, %u],\n"
        "  \"binding_count\": %u,\n  \"bindings\": [\n",
        r->entry_point, r->workgroup_size[0], r->workgroup_size[1],
        r->workgroup_size[2], r->binding_count);
    if (written < 0 || (size_t)written >= sizeof(json)) {
        fprintf(stderr, "reflection exceeds buffer\n"); goto cleanup;
    }
    size_t json_length = (size_t)written;
    for (uint32_t i = 0; i < r->binding_count; ++i) {
        const DoeCompilerBinding *binding = &r->bindings[i];
        written = snprintf(json + json_length, sizeof(json) - json_length,
            "    {\"name\": \"%s\", \"group\": %u, \"binding\": %u, "
            "\"scalar_type\": \"%s\", \"access\": \"%s\", "
            "\"element_stride\": %u, \"runtime_sized\": %s}%s\n",
            binding->name, binding->group, binding->binding,
            binding->scalar == DOE_COMPILER_F32 ? "f32" : "u32",
            binding->access == DOE_COMPILER_READ ? "read" : "read_write",
            binding->element_stride, binding->runtime_sized != 0u ? "true" : "false",
            i + 1u == r->binding_count ? "" : ",");
        if (written < 0 || (size_t)written >= sizeof(json) - json_length) {
            fprintf(stderr, "reflection exceeds buffer\n"); goto cleanup;
        }
        json_length += (size_t)written;
    }
    written = snprintf(json + json_length, sizeof(json) - json_length, "  ]\n}\n");
    if (written < 0 || (size_t)written >= sizeof(json) - json_length) {
        fprintf(stderr, "reflection exceeds buffer\n"); goto cleanup;
    }
    json_length += (size_t)written;
    unsigned char binary[DOE_COMPILER_MAX_WORDS * 4u];
    for (size_t i = 0; i < result.word_count; ++i)
        for (unsigned b = 0; b < 4u; ++b) binary[i * 4u + b] = (unsigned char)(words[i] >> (b * 8u));
    binary_fd = temporary(output, binary_temp);
    if (binary_fd < 0) { perror("create binary temporary"); goto cleanup; }
    binary_created = 1;
    json_fd = temporary(reflection, json_temp);
    if (json_fd < 0) { perror("create reflection temporary"); goto cleanup; }
    json_created = 1;
    if (write_all(binary_fd, binary, result.word_count * 4u) < 0 ||
        write_all(json_fd, (const unsigned char *)json, json_length) < 0) { perror("write artifacts"); goto cleanup; }
    if (close(binary_fd) < 0) { binary_fd = -1; perror("close binary"); goto cleanup; }
    binary_fd = -1;
    if (close(json_fd) < 0) { json_fd = -1; perror("close reflection"); goto cleanup; }
    json_fd = -1;
    /* Atomic no-clobber publication of each file, rollback the first if second fails.
     * Not a cross-filesystem crash transaction; no existing destination is replaced. */
    if (link(binary_temp, output) < 0) { perror("publish binary"); goto cleanup; }
    binary_published = 1;
    if (link(json_temp, reflection) < 0) { perror("publish reflection"); goto cleanup; }
    json_published = 1;
    status = 0;
cleanup:
    if (input_fd >= 0) (void)close(input_fd);
    if (binary_fd >= 0) (void)close(binary_fd);
    if (json_fd >= 0) (void)close(json_fd);
    if (status != 0 && binary_published) (void)unlink(output);
    if (status != 0 && json_published) (void)unlink(reflection);
    if (binary_created) (void)unlink(binary_temp);
    if (json_created) (void)unlink(json_temp);
    return status;
}
