/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _POSIX_C_SOURCE 200809L
#include "job.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("doe-emit-wgsl-c SOURCE.wgsl OUTPUT.wgsl");
        return 0;
    }
    if (argc != 3) {
        fprintf(stderr, "usage: doe-emit-wgsl-c SOURCE.wgsl OUTPUT.wgsl\n");
        return 2;
    }
    FILE *input = fopen(argv[1], "rb");
    if (input == NULL) { perror("open WGSL source"); return 1; }
    char *source = malloc(DOE_COMPILER_MAX_SOURCE_BYTES + 1u);
    if (source == NULL) { (void)fclose(input); return 1; }
    const size_t count = fread(source, 1u, DOE_COMPILER_MAX_SOURCE_BYTES + 1u, input);
    const bool read_failed = ferror(input) != 0;
    const int closed = fclose(input);
    if (read_failed || closed != 0 || count > DOE_COMPILER_MAX_SOURCE_BYTES) {
        fprintf(stderr, "WGSL input failed or exceeded 65536 bytes\n");
        free(source);
        return 1;
    }
    WgslJobResult result;
    compiler_wgsl_job(source, count, true, &result);
    free(source);
    if (result.diagnostic.status != DOE_COMPILER_OK) {
        fprintf(stderr, "%s:%u:%u: %s (byte %zu)\n", argv[1], result.diagnostic.line,
            result.diagnostic.column, result.diagnostic.message, result.diagnostic.offset);
        return 1;
    }
    const size_t path_length = strlen(argv[2]);
    if (path_length > SIZE_MAX - sizeof(".tmp.XXXXXX")) { free(result.wgsl); return 1; }
    char *temporary = malloc(path_length + sizeof(".tmp.XXXXXX"));
    if (temporary == NULL) { free(result.wgsl); return 1; }
    memcpy(temporary, argv[2], path_length);
    memcpy(temporary + path_length, ".tmp.XXXXXX", sizeof(".tmp.XXXXXX"));
    int fd = mkstemp(temporary);
    const bool created = fd >= 0;
    int status = 1;
    if (fd < 0) goto finish;
    size_t written = 0u;
    while (written < result.length) {
        const ssize_t n = write(fd, result.wgsl + written, result.length - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto finish;
        written += (size_t)n;
    }
    if (close(fd) != 0) { fd = -1; goto finish; }
    fd = -1;
    if (rename(temporary, argv[2]) != 0) goto finish;
    status = 0;
    fprintf(stderr, "WGSL: %zu typed rewrites, %zu bytes\n", result.rewrites, result.length);
finish:
    if (fd >= 0) (void)close(fd);
    if (status != 0) {
        perror("publish WGSL");
        if (created) (void)unlink(temporary);
    }
    free(temporary);
    free(result.wgsl);
    return status;
}
