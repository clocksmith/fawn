/* SPDX-License-Identifier: MIT */
#ifndef DOE_D3D12_SHADER_H
#define DOE_D3D12_SHADER_H
#include "doe_d3d12.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Private official Windows shader compiler adapter. HLSL generation, reflection, entry selection and
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
    doe_dx_shader_stage stage; /* Legacy/default cs_6_0, vs_6_0 or ps_6_0. */
    bool has_entry_index;
    uint32_t entry_index; /* Compiler-owned DOE_HLSL_ENTRY value, never inferred. */
} doe_dx_shader_options;
/* Additive compiler-owned VS interface projection. Existing options and compile
 * ABI stay unchanged. The uint32 mask selects numeric user-output locations;
 * zero is valid, reserved must be zero, and only VERTEX accepts projection.
 * The compiler emits/documented guards and owns declaration/assignment meaning;
 * this SDK adapter only passes its named define before signature packing. */
typedef struct doe_dx_shader_projection {
    uint32_t interstage_mask, reserved;
} doe_dx_shader_projection;
/* Additive target requirements. Existing compile APIs remain SM6.0. Native16
 * requires SM6.2 and the actual DXC 16-bit-types switch; no implicit promotion. */
enum { DOE_DX_SHADER_NATIVE16 = 1u };
typedef struct doe_dx_shader_requirements {
    uint32_t shader_model; /* Exactly 501, 600 or 620. Explicit501 is compute-only FXC. */
    uint32_t flags; /* DOE_DX_SHADER_NATIVE16 only; unknown bits reject. */
    uint32_t reserved[2];
} doe_dx_shader_requirements;
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
 * DXC uses -HV2021 -Ges -WX -Gis and the requested stage/entry/optional index.
 * Both graphics profiles explicitly use prefix-stable signature packing; this
 * is a target ABI policy, not a new application compilation flag.
 * Inputs are borrowed only for this call. Output/error storage is live, aligned
 * and disjoint from each other and the inputs.
 * Failure leaves *out untouched and may replace error. Legacy success owns one DXC
 * blob and its explicit module until release. Caller errno, LastError, standard
 * fenv and x86 MXCSR are preserved across this SDK call; this is not WGSL numeric
 * normalization. Release preserves the same host state. Serialize release
 * against every borrowed view. */
doe_dx_status doe_dx_shader_compile(const doe_dx_shader_options *options,
    doe_dx_shader **out, doe_dx_shader_error *error);
/* Same ownership/host-state/failure contract as compile. projection is required;
 * wrong stage/reserved fields reject before DXC and leave *out unchanged. */
doe_dx_status doe_dx_shader_compile_projected(const doe_dx_shader_options *options,
    const doe_dx_shader_projection *projection,doe_dx_shader **out,doe_dx_shader_error *error);
/* requirements is required; projection is optional (vertex only when present).
 * Explicit501 selects official FXC cs_5_1 with strict IEEE arithmetic, warnings
 * as errors and optimization level3; no projection/native16. Module path must
 * name that selected compiler. There is no retry/fallback between compilers.
 * All other ownership, diagnostics and host-state rules are compile's rules. */
doe_dx_status doe_dx_shader_compile_required(const doe_dx_shader_options *options,
    const doe_dx_shader_projection *projection, const doe_dx_shader_requirements *requirements,
    doe_dx_shader **out, doe_dx_shader_error *error);
/* NULL shader returns NULL and size zero. Otherwise immutable bytes are borrowed
 * while the caller owns this object; native PSO creation copies those bytes. */
const uint8_t *doe_dx_shader_bytes(const doe_dx_shader *shader, size_t *size);
void doe_dx_shader_release(doe_dx_shader *shader);

#ifdef __cplusplus
}
#endif
#endif
