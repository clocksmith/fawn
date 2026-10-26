// SPDX-License-Identifier: MIT
#include "tsir_csl_common.h"

#include <stdio.h>
#include <string.h>

void tsir_csl_default_config(TsirCslConfig *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->var_prefix = "tsir_";
    out->kv_cache_pe_strategy = TSIR_CSL_KV_FULL_PER_PE;
    out->attention_pe_strategy = TSIR_CSL_ATTN_FULL_PER_PE;
    out->attention_pe_id_source = TSIR_CSL_ATTN_ID_TILE_PARAM;
}

const char *tsir_csl_elem_name(TsirCslElem elem) {
    switch (elem) {
        case TSIR_CSL_F32:
            return "f32";
        case TSIR_CSL_F16:
            return "f16";
        case TSIR_CSL_BF16:
            return "bf16";
        case TSIR_CSL_I32:
            return "i32";
        case TSIR_CSL_U32:
            return "u32";
        default:
            return NULL;
    }
}

int tsir_csl_is_supported_compute_elem(TsirCslElem elem) {
    return elem == TSIR_CSL_F32 || elem == TSIR_CSL_F16;
}

CslErr tsir_csl_require_supported_compute_elem(TsirCslElem elem) {
    if (!tsir_csl_is_supported_compute_elem(elem)) {
        return CSL_ERR_UNSUPPORTED;
    }
    return CSL_OK;
}

CslErr tsir_csl_resolve_config(const TsirCslKernelDesc *desc,
    TsirCslConfig *default_storage, const TsirCslConfig **out_config) {
    size_t i = 0;
    if (desc == NULL || out_config == NULL) {
        return CSL_ERR_INVALID;
    }
    if (desc->binding_count > 0 && desc->bindings == NULL) {
        return CSL_ERR_INVALID;
    }
    for (i = 0; i < desc->binding_count; i++) {
        if (desc->bindings[i].name == NULL ||
            desc->bindings[i].name[0] == '\0') {
            return CSL_ERR_INVALID;
        }
    }
    if (desc->config == NULL) {
        if (default_storage == NULL) {
            return CSL_ERR_INVALID;
        }
        tsir_csl_default_config(default_storage);
        *out_config = default_storage;
        return CSL_OK;
    }
    if (desc->config->var_prefix == NULL) {
        return CSL_ERR_INVALID;
    }
    *out_config = desc->config;
    return CSL_OK;
}

CslErr tsir_csl_binding_for_role(const TsirCslKernelDesc *desc,
    TsirCslRole role, const TsirCslBinding **out) {
    size_t i = 0;
    if (desc == NULL || out == NULL) {
        return CSL_ERR_INVALID;
    }
    for (i = 0; i < desc->binding_count; i++) {
        if (desc->bindings[i].role == role) {
            *out = &desc->bindings[i];
            return CSL_OK;
        }
    }
    return CSL_ERR_UNSUPPORTED;
}

CslErr tsir_csl_require_elem(const TsirCslBinding *binding,
    TsirCslElem elem) {
    if (binding == NULL) {
        return CSL_ERR_INVALID;
    }
    if (binding->elem != elem) {
        return CSL_ERR_UNSUPPORTED;
    }
    return CSL_OK;
}

CslErr tsir_csl_write_buffer_array(CslBuf *out, const char *prefix,
    const char *name, const char *extent, const char *elem_type) {
    if (out == NULL || prefix == NULL || name == NULL ||
        extent == NULL || elem_type == NULL) {
        return CSL_ERR_INVALID;
    }
    return csl_buf_printf(out,
        "var %s%s: [%s]%s = @zeros([%s]%s);\n", prefix, name, extent,
        elem_type, extent, elem_type);
}

CslErr tsir_csl_write_buffer_pointer(CslBuf *out, const char *prefix,
    const char *name, const char *elem_type) {
    if (out == NULL || prefix == NULL || name == NULL ||
        elem_type == NULL) {
        return CSL_ERR_INVALID;
    }
    return csl_buf_printf(out, "var %s%s_ptr: [*]%s = &%s%s;\n",
        prefix, name, elem_type, prefix, name);
}

CslErr tsir_csl_write_export_symbol(CslBuf *out, const char *prefix,
    const char *name) {
    if (out == NULL || prefix == NULL || name == NULL) {
        return CSL_ERR_INVALID;
    }
    return csl_buf_printf(out, "    @export_symbol(%s%s_ptr, \"%s\");\n",
        prefix, name, name);
}

CslErr tsir_csl_write_sqrt_nr(CslBuf *out, TsirCslElem elem) {
    const char *ty = NULL;
    CslErr err = CSL_OK;
    if (out == NULL) {
        return CSL_ERR_INVALID;
    }
    ty = tsir_csl_elem_name(elem);
    if (ty == NULL) {
        return CSL_ERR_INVALID;
    }
    if (elem == TSIR_CSL_F32) {
        err = csl_buf_puts(out, "fn sqrt_nr(x: f32) f32 {\n");
        if (err != CSL_OK) {
            return err;
        }
        err = csl_buf_puts(out, "    const y0: f32 = math.sqrt(x);\n");
        if (err != CSL_OK) {
            return err;
        }
        err = csl_buf_puts(out, "    return 0.5 * (y0 + x / y0);\n");
        if (err != CSL_OK) {
            return err;
        }
        return csl_buf_puts(out, "}\n\n");
    }
    err = csl_buf_printf(out, "fn sqrt_nr(x: %s) %s {\n", ty, ty);
    if (err != CSL_OK) {
        return err;
    }
    err = csl_buf_puts(out, "    const x32: f32 = @as(f32, x);\n");
    if (err != CSL_OK) {
        return err;
    }
    err = csl_buf_puts(out, "    const y0: f32 = math.sqrt(x32);\n");
    if (err != CSL_OK) {
        return err;
    }
    err = csl_buf_puts(out,
        "    const refined: f32 = 0.5 * (y0 + x32 / y0);\n");
    if (err != CSL_OK) {
        return err;
    }
    err = csl_buf_printf(out, "    return @as(%s, refined);\n", ty);
    if (err != CSL_OK) {
        return err;
    }
    return csl_buf_puts(out, "}\n\n");
}

CslErr tsir_csl_write_sys_mod(CslBuf *out) {
    if (out == NULL) {
        return CSL_ERR_INVALID;
    }
    return csl_buf_puts(out,
        "const sys_mod = @import_module(\"<memcpy/memcpy>\", memcpy_params);\n");
}

CslErr tsir_csl_write_math_import(CslBuf *out) {
    if (out == NULL) {
        return CSL_ERR_INVALID;
    }
    return csl_buf_puts(out, "const math = @import_module(\"<math>\");\n");
}

CslErr tsir_csl_write_compute_tail(CslBuf *out, const char *prefix,
    const char *const *names, size_t count) {
    CslErr err = CSL_OK;
    size_t i = 0;
    if (out == NULL || prefix == NULL || count == 0 || names == NULL) {
        return CSL_ERR_INVALID;
    }
    for (i = 0; i < count; i++) {
        if (names[i] == NULL) {
            return CSL_ERR_INVALID;
        }
    }
    err = csl_buf_puts(out, "    sys_mod.unblock_cmd_stream();\n");
    if (err != CSL_OK) {
        return err;
    }
    err = csl_buf_puts(out, "}\n\n");
    if (err != CSL_OK) {
        return err;
    }
    err = csl_buf_puts(out, "comptime {\n");
    if (err != CSL_OK) {
        return err;
    }
    for (i = 0; i < count; i++) {
        err = tsir_csl_write_export_symbol(out, prefix, names[i]);
        if (err != CSL_OK) {
            return err;
        }
    }
    err = csl_buf_puts(out, "    @export_symbol(compute);\n");
    if (err != CSL_OK) {
        return err;
    }
    return csl_buf_puts(out, "}\n");
}

CslErr tsir_csl_format_scientific(char *out, size_t cap, double value) {
    int n = 0;
    if (out == NULL || cap == 0) {
        return CSL_ERR_INVALID;
    }
    n = snprintf(out, cap, "%e", value);
    if (n < 0 || (size_t)n >= cap) {
        return CSL_ERR_TOO_LARGE;
    }
    return CSL_OK;
}
