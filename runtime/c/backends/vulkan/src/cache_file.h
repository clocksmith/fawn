/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_CACHE_FILE_H
#define DOE_VK_CACHE_FILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { DOE_VK_CACHE_MAX_BYTES = 32 * 1024 * 1024 };
bool doe_vk_cache_directory(const char *directory);
bool doe_vk_cache_read(const char *path, uint8_t **bytes, size_t *size);
bool doe_vk_cache_replace(const char *path, const uint8_t *bytes, size_t size);
#endif
