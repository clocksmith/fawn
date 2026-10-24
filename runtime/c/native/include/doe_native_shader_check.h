/* SPDX-License-Identifier: MIT
 * Doe device-less WGSL check extension ABI (not webgpu.h). Signatures match the
 * retained Zig exports consumed by runtime/bridge/webgpu-addon/doe_napi_shader.c.
 * Listed in ../EXTENSION_SYMBOLS.txt, never resolved by wgpuGetProcAddress. */
#ifndef DOE_NATIVE_SHADER_CHECK_H
#define DOE_NATIVE_SHADER_CHECK_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(__GNUC__) || defined(__clang__)
#define DOE_NATIVE_EXTENSION __attribute__((visibility("default")))
#else
#define DOE_NATIVE_EXTENSION
#endif

/* Runs the whole-module validation and emission createShaderModule runs for this
 * platform's default target, without a device. 1 when the module is accepted, 0
 * with a diagnostic otherwise. Source need not be NUL terminated. */
DOE_NATIVE_EXTENSION uint32_t doeNativeCheckShaderSource(const char *source, size_t size);
/* The calling thread's latest check diagnostic, empty after an accepted check.
 * Copies return the full length and write at most out_size - 1 bytes plus NUL. */
DOE_NATIVE_EXTENSION size_t doeNativeCopyLastErrorMessage(char *out, size_t out_size);
DOE_NATIVE_EXTENSION size_t doeNativeCopyLastErrorStage(char *out, size_t out_size);
DOE_NATIVE_EXTENSION size_t doeNativeCopyLastErrorKind(char *out, size_t out_size);
/* 1-based source position of that diagnostic, 0 when unknown. */
DOE_NATIVE_EXTENSION uint32_t doeNativeGetLastErrorLine(void);
DOE_NATIVE_EXTENSION uint32_t doeNativeGetLastErrorColumn(void);
#ifdef __cplusplus
}
#endif
#endif
