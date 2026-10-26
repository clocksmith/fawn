/* SPDX-License-Identifier: MIT */
#ifndef DOE_TSIR_ORACLE_ALLOC_H
#define DOE_TSIR_ORACLE_ALLOC_H
#include <stdlib.h>
void *oracle_test_malloc(size_t size);
void *oracle_test_calloc(size_t count, size_t size);
void *oracle_test_realloc(void *pointer, size_t size);
void oracle_test_free(void *pointer);
#define malloc oracle_test_malloc
#define calloc oracle_test_calloc
#define realloc oracle_test_realloc
#define free oracle_test_free
#endif
