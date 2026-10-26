/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_FILE_H
#define DOE_TSIR_FILE_H
#include "tsir_schema.h"
#include <stdio.h>
/* Private host I/O owner. UTF-8 paths on Windows; regular binary files only.
 * Success transfers the stream to the caller (fclose); failure leaves out
 * untouched and retains the first errno across resource cleanup. */
typedef struct TsirFileStamp {
    uintmax_t size;
    int64_t modified;
} TsirFileStamp;
TsirStatus tsir_file_open(const char *path, FILE **out, TsirFileStamp *stamp);
TsirStatus tsir_file_stamp(FILE *stream, TsirFileStamp *stamp);
#endif
