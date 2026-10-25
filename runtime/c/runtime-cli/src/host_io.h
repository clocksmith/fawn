/* SPDX-License-Identifier: MIT */
#ifndef DOE_C_HOST_IO_H
#define DOE_C_HOST_IO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Private output until close publishes a complete allocation. A successful close
 * gives the caller ownership of bytes (NUL-terminated; size excludes the NUL).
 * Failure leaves caller outputs unchanged. Discard releases any pending output. */
typedef struct doe_host_output {
    FILE *file;
    char *bytes;
    size_t size;
} doe_host_output;
FILE *doe_host_output_open(doe_host_output *output);
bool doe_host_output_close(doe_host_output *output, char **bytes, size_t *size);
void doe_host_output_discard(doe_host_output *output);

char *doe_host_strdup(const char *text);
FILE *doe_host_fopen(const char *path, const char *mode); /* UTF-8 path on every host. */
const char *doe_host_basename(const char *path);
int doe_host_casecmp(const char *left, const char *right); /* ASCII case folding. */
uint64_t doe_host_monotonic_ns(void);
uint64_t doe_host_realtime_ns(void);
void doe_host_pause_ns(uint64_t duration);
bool doe_host_regular_file(const char *path);
bool doe_host_path_separator(char value);
bool doe_host_make_parent_directories(const char *path);
#endif
