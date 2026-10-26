// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_host_plan_tool_config.zig (UNTESTED).
// Status: partial. All pure validation and mapping rules are ported.
// The three JSON parse entry points are NOT ported: they return
// CSL_ERR_UNSUPPORTED rather than claiming a parse they do not do.
// A caller that parses bundle JSON elsewhere can validate the resulting
// typed values with the validators below. Parity is NOT verified.
#ifndef DOE_CSL_SPATIAL_HOST_PLAN_TOOL_CONFIG_H
#define DOE_CSL_SPATIAL_HOST_PLAN_TOOL_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "csl_error.h"
#include "csl_spatial_bindings.h"

typedef enum CslSpatialQuantFormat {
    CSL_SPATIAL_QUANT_F16 = 0,
    CSL_SPATIAL_QUANT_Q4K = 1,
    CSL_SPATIAL_QUANT_Q8_0 = 2
} CslSpatialQuantFormat;

typedef enum CslSpatialWeightDtype {
    CSL_SPATIAL_WEIGHT_F16 = 0,
    CSL_SPATIAL_WEIGHT_U8_Q4K = 1,
    CSL_SPATIAL_WEIGHT_U8_Q8 = 2
} CslSpatialWeightDtype;

/*
 * Mirror of host.ModelConfig. Zig optionals become a value plus a
 * has_* flag. quant_format is required in Zig; the JSON default rules
 * (ffn_expansion_factor 4, ffn_matrix_count 3, partial_rotary_factor
 * 1.0) are documented here but are the JSON parser's job, so a caller
 * constructing this struct directly must set them explicitly.
 */
typedef struct CslSpatialModelConfig {
    uint32_t hidden_dim;
    uint32_t num_heads;
    uint32_t head_dim;
    int has_linear_key_head_dim;
    uint32_t linear_key_head_dim;
    int has_linear_value_head_dim;
    uint32_t linear_value_head_dim;
    int has_linear_conv_kernel_dim;
    uint32_t linear_conv_kernel_dim;
    int has_global_head_dim;
    uint32_t global_head_dim;
    int has_num_key_value_heads;
    uint32_t num_key_value_heads;
    uint32_t num_layers;
    uint32_t vocab_size;
    uint32_t max_seq_len;
    CslSpatialQuantFormat quant_format;
    uint32_t ffn_expansion_factor;
    uint32_t ffn_matrix_count;
    int has_ple_width;
    uint32_t ple_width;
    int has_ple_vocab_size;
    uint32_t ple_vocab_size;
    float partial_rotary_factor;
    int has_mrope_section;
    uint32_t mrope_section[3];
} CslSpatialModelConfig;

typedef struct CslSpatialWeightQuant {
    const char *format;
    const char *storage_dtype;
    const char *source_dtype; /* NULL when absent */
    int has_block_size_elements;
    uint32_t block_size_elements;
    int has_block_size_bytes;
    uint32_t block_size_bytes;
    const char *encoding; /* NULL when absent */
} CslSpatialWeightQuant;

typedef struct CslSpatialWeightMapping {
    const char *shard_name;
    const char *shard_path;
    const char *shard_sha256; /* must pass csl_spatial_config_check_sha256 */
    const char *pe_buffer;
    uint32_t pe_start;
    uint32_t pe_end; /* must be strictly greater than pe_start */
    CslSpatialWeightDtype dtype;
    const char *tensor_name;
    uint64_t tensor_offset_bytes;
    const uint64_t *tensor_shape;
    size_t tensor_shape_count; /* must be at least 1, every dim at least 1 */
    CslSpatialWeightQuant quant;
} CslSpatialWeightMapping;

/* Pure mappings, mirroring the Zig helpers exactly. */
CslErr csl_spatial_config_parse_quant_format(const char *raw,
                                             CslSpatialQuantFormat *out);
CslErr csl_spatial_config_parse_weight_dtype(const char *raw,
                                             CslSpatialWeightDtype *out);
CslErr csl_spatial_config_admit_activation_dtype(const char *dtype);
CslErr csl_spatial_config_activation_scalar(const char *dtype,
                                            CslSpatialScalar *out);
CslErr csl_spatial_config_check_sha256(const char *raw);
CslErr csl_spatial_config_check_pe_range(uint32_t start, uint32_t end);
CslErr csl_spatial_config_check_weight_shape(const uint64_t *shape,
                                             size_t count);
CslErr csl_spatial_config_check_weight_mapping(
    const CslSpatialWeightMapping *mapping);

/* NOT PORTED JSON entry points. Always CSL_ERR_UNSUPPORTED. */
CslErr csl_spatial_config_parse_bundle_model_config(
    const char *json, size_t len, CslSpatialModelConfig *out, int *found);
CslErr csl_spatial_config_parse_bundle_activation_dtype(
    const char *json, size_t len, const char **out_dtype);
CslErr csl_spatial_config_parse_bundle_weight_mappings(
    const char *json, size_t len, CslSpatialWeightMapping *out,
    size_t capacity, size_t *out_count);

#endif
