/* SPDX-License-Identifier: MIT */
#ifndef DOE_D3D12_SHADER_H
#define DOE_D3D12_SHADER_H
#include "doe_d3d12.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Private official-DXC adapter. HLSL generation, reflection, entry selection and
 * device requirements belong to the compiler/frontend, never this SDK shim. */
typedef struct doe_dx_shader doe_dx_shader;
typedef enum doe_dx_shader_stage {
    DOE_DX_SHADER_COMPUTE, DOE_DX_SHADER_VERTEX, DOE_DX_SHADER_FRAGMENT
} doe_dx_shader_stage;
typedef struct doe_dx_shader_options {
    const char *compiler_module_path; /* Fully qualified UTF-8 Windows DLL path. */
    const char *source; /* Borrowed exact UTF-8 bytes, no embedded NUL. */
    size_t source_size;
    const char *entry; /* Borrowed ASCII identifier, 1..63 characters. */
    doe_dx_shader_stage stage; /* cs_6_0, vs_6_0 or ps_6_0, respectively. */
    bool has_entry_index;
    uint32_t entry_index; /* Compiler-owned DOE_HLSL_ENTRY value, never inferred. */
} doe_dx_shader_options;
enum { DOE_DX_SHADER_MESSAGE_CAPACITY = 1024 };
typedef struct doe_dx_shader_error {
    doe_dx_status status;
    int32_t native_code; /* HRESULT or Win32 error, identified by operation. */
    const char *operation; /* Static adapter string, not a borrowed DXC string. */
    char message[DOE_DX_SHADER_MESSAGE_CAPACITY]; /* Owned bounded UTF-8 bytes. */
    size_t message_size; /* Excludes the NUL terminator. */
    bool truncated;
} doe_dx_shader_error;

/* No callbacks, shell, arbitrary flags, include handler or filesystem includes.
 * Uses -HV2021 -Ges -WX -Gis and exactly the requested stage/entry/optional index.
 * Inputs are borrowed only for this call. Output/error storage is live, aligned
 * and disjoint from each other and the inputs.
 * Failure leaves *out untouched and may replace error. Success owns one DXC
 * blob and its explicit module until release. Caller errno, LastError, standard
 * fenv and x86 MXCSR are preserved across this SDK call; this is not WGSL numeric
 * normalization. Serialize release against every borrowed view. */
doe_dx_status doe_dx_shader_compile(const doe_dx_shader_options *options,
    doe_dx_shader **out, doe_dx_shader_error *error);
/* NULL shader returns NULL and size zero. Otherwise immutable bytes are borrowed
 * while the caller owns this object; native PSO creation copies those bytes. */
const uint8_t *doe_dx_shader_bytes(const doe_dx_shader *shader, size_t *size);
void doe_dx_shader_release(doe_dx_shader *shader);

#ifdef __cplusplus
}
#endif
#endif
