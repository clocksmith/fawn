// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_host_plan_tool_config.zig (UNTESTED).
// JSON parsing is NOT ported; see the header. Parity NOT verified.
#include "csl_spatial_host_plan_tool_config.h"

#include <string.h>

CslErr csl_spatial_config_parse_quant_format(const char *raw,
                                             CslSpatialQuantFormat *out) {
    if (!raw || !out) {
        return CSL_ERR_INVALID;
    }
    if (strcmp(raw, "f16") == 0) {
        *out = CSL_SPATIAL_QUANT_F16;
    } else if (strcmp(raw, "q4k") == 0) {
        *out = CSL_SPATIAL_QUANT_Q4K;
    } else if (strcmp(raw, "q8_0") == 0) {
        *out = CSL_SPATIAL_QUANT_Q8_0;
    } else {
        return CSL_ERR_INVALID;
    }
    return CSL_OK;
}

CslErr csl_spatial_config_parse_weight_dtype(const char *raw,
                                             CslSpatialWeightDtype *out) {
    if (!raw || !out) {
        return CSL_ERR_INVALID;
    }
    if (strcmp(raw, "f16") == 0) {
        *out = CSL_SPATIAL_WEIGHT_F16;
    } else if (strcmp(raw, "u8_q4k") == 0) {
        *out = CSL_SPATIAL_WEIGHT_U8_Q4K;
    } else if (strcmp(raw, "u8_q8") == 0) {
        *out = CSL_SPATIAL_WEIGHT_U8_Q8;
    } else {
        return CSL_ERR_INVALID;
    }
    return CSL_OK;
}

CslErr csl_spatial_config_admit_activation_dtype(const char *dtype) {
    if (!dtype) {
        return CSL_OK;
    }
    if (strcmp(dtype, "f32") == 0 || strcmp(dtype, "f16") == 0) {
        return CSL_OK;
    }
    return CSL_ERR_INVALID;
}

CslErr csl_spatial_config_activation_scalar(const char *dtype,
                                            CslSpatialScalar *out) {
    if (!out) {
        return CSL_ERR_INVALID;
    }
    if (!dtype) {
        *out = CSL_SPATIAL_SCALAR_F32;
        return CSL_OK;
    }
    if (strcmp(dtype, "f32") == 0) {
        *out = CSL_SPATIAL_SCALAR_F32;
        return CSL_OK;
    }
    if (strcmp(dtype, "f16") == 0) {
        *out = CSL_SPATIAL_SCALAR_F16;
        return CSL_OK;
    }
    return CSL_ERR_INVALID;
}

CslErr csl_spatial_config_check_sha256(const char *raw) {
    size_t i;
    if (!raw) {
        return CSL_ERR_INVALID;
    }
    if (strlen(raw) != 64u) {
        return CSL_ERR_INVALID;
    }
    for (i = 0; i < 64u; i++) {
        char c = raw[i];
        int digit = c >= '0' && c <= '9';
        int lower_hex = c >= 'a' && c <= 'f';
        if (!digit && !lower_hex) {
            return CSL_ERR_INVALID;
        }
    }
    return CSL_OK;
}

CslErr csl_spatial_config_check_pe_range(uint32_t start, uint32_t end) {
    return (end > start) ? CSL_OK : CSL_ERR_INVALID;
}

CslErr csl_spatial_config_check_weight_shape(const uint64_t *shape,
                                             size_t count) {
    size_t i;
    if (!shape || count == 0) {
        return CSL_ERR_INVALID;
    }
    for (i = 0; i < count; i++) {
        if (shape[i] == 0) {
            return CSL_ERR_INVALID;
        }
    }
    return CSL_OK;
}

CslErr csl_spatial_config_check_weight_mapping(
    const CslSpatialWeightMapping *mapping) {
    CslErr err;
    if (!mapping) {
        return CSL_ERR_INVALID;
    }
    if (!mapping->shard_name || !mapping->shard_path ||
        !mapping->pe_buffer || !mapping->tensor_name ||
        !mapping->quant.format || !mapping->quant.storage_dtype) {
        return CSL_ERR_INVALID;
    }
    err = csl_spatial_config_check_sha256(mapping->shard_sha256);
    if (err) {
        return err;
    }
    err = csl_spatial_config_check_pe_range(mapping->pe_start,
                                            mapping->pe_end);
    if (err) {
        return err;
    }
    return csl_spatial_config_check_weight_shape(mapping->tensor_shape,
                                                mapping->tensor_shape_count);
}

CslErr csl_spatial_config_parse_bundle_model_config(
    const char *json, size_t len, CslSpatialModelConfig *out, int *found) {
    (void)json;
    (void)len;
    (void)out;
    (void)found;
    return CSL_ERR_UNSUPPORTED;
}

CslErr csl_spatial_config_parse_bundle_activation_dtype(
    const char *json, size_t len, const char **out_dtype) {
    (void)json;
    (void)len;
    (void)out_dtype;
    return CSL_ERR_UNSUPPORTED;
}

CslErr csl_spatial_config_parse_bundle_weight_mappings(
    const char *json, size_t len, CslSpatialWeightMapping *out,
    size_t capacity, size_t *out_count) {
    (void)json;
    (void)len;
    (void)out;
    (void)capacity;
    (void)out_count;
    return CSL_ERR_UNSUPPORTED;
}
