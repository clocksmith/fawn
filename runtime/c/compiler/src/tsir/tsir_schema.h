/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_SCHEMA_H
#define DOE_TSIR_SCHEMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* TSIR schema types, mirroring runtime/zig/src/compiler/tsir/schema.zig.
 * Text is caller-owned. An associated *_length greater than zero selects that
 * exact byte span (including embedded NUL); zero preserves the legacy strlen
 * convention. Represent an empty value with "". Nonempty explicit spans need
 * not be terminated. A NULL text pointer is invalid unless documented optional.
 * Arrays use pointer/count pairs, with NULL valid only for count zero.
 * Frontend/planner-owned results keep exact lengths and free their text through
 * the matching free function. Enum tags retain the Zig declaration order. */

/* Shared byte-view helpers; zero length means legacy NUL-terminated input. */
size_t tsir_text_length(const char *text, size_t length);
bool tsir_text_equal(const char *left, size_t left_length,
    const char *right, size_t right_length);
bool tsir_text_starts_with(const char *text, size_t length, const char *prefix);
char *tsir_text_duplicate(const char *text, size_t length);

typedef enum TsirStatus {
    TSIR_OK = 0,
    TSIR_ERR_NOMEM = 1,
    TSIR_ERR_INVALID = 2,
    TSIR_ERR_UNSUPPORTED = 3,
    TSIR_ERR_OVERFLOW = 4,
    TSIR_ERR_RANGE = 5,
    TSIR_ERR_REJECTED = 6
} TsirStatus;

const char *tsir_status_name(TsirStatus status);

#define TSIR_CONTRACT_VERSION 1u

typedef enum TsirExactnessClass {
    TSIR_EXACTNESS_BIT_EXACT_SOLO = 0,
    TSIR_EXACTNESS_ALGORITHM_EXACT = 1,
    TSIR_EXACTNESS_TOLERANCE_BOUNDED = 2
} TsirExactnessClass;

typedef enum TsirAlgorithmExactInvariant {
    TSIR_INVARIANT_REDUCTION_ORDER = 0,
    TSIR_INVARIANT_TREE_SHAPE = 1,
    TSIR_INVARIANT_ACCUM_DTYPE = 2,
    TSIR_INVARIANT_ASSOCIATIVITY_GROUPING = 3
} TsirAlgorithmExactInvariant;

typedef enum TsirRejectionReason {
    TSIR_REJECT_BINDING_TYPE_UNLOWERABLE = 0,
    TSIR_REJECT_SUBGROUP_UNLOWERABLE = 1,
    TSIR_REJECT_PE_BUDGET_EXHAUSTED = 2,
    TSIR_REJECT_COLLECTIVE_NOT_REPRESENTABLE = 3,
    TSIR_REJECT_DEPENDENCE_UNANALYZABLE = 4,
    TSIR_REJECT_SOURCE_NOT_AFFINE = 5,
    TSIR_REJECT_TARGET_UNFIT = 6
} TsirRejectionReason;

typedef enum TsirKernelFamilyHint {
    TSIR_HINT_UNKNOWN = 0,
    TSIR_HINT_ELEMENTWISE = 1,
    TSIR_HINT_REDUCTION = 2,
    TSIR_HINT_GATHER = 3,
    TSIR_HINT_RMS_NORM = 4,
    TSIR_HINT_FUSED_GEMV = 5,
    TSIR_HINT_PREFILL_Q4K_GEMV = 6,
    TSIR_HINT_TILED_MATMUL = 7,
    TSIR_HINT_ATTENTION_DECODE = 8,
    TSIR_HINT_ATTENTION_TILED = 9,
    TSIR_HINT_ROPE = 10,
    TSIR_HINT_DEQUANT = 11,
    TSIR_HINT_SAMPLE = 12,
    TSIR_HINT_EMBED = 13
} TsirKernelFamilyHint;

typedef enum TsirScalarKind {
    TSIR_SCALAR_F32 = 0,
    TSIR_SCALAR_F16 = 1,
    TSIR_SCALAR_BF16 = 2,
    TSIR_SCALAR_I32 = 3,
    TSIR_SCALAR_U32 = 4
} TsirScalarKind;

typedef enum TsirReductionAssociativity {
    TSIR_ASSOC_STRICT_ORDERED = 0,
    TSIR_ASSOC_ASSOCIATIVE_ALLOWED = 1
} TsirReductionAssociativity;

typedef enum TsirNanInfPolicy {
    TSIR_NANINF_PROPAGATE = 0,
    TSIR_NANINF_QUIET_MASK = 1
} TsirNanInfPolicy;

typedef enum TsirReductionOp {
    TSIR_REDUCTION_SUM = 0,
    TSIR_REDUCTION_PRODUCT = 1,
    TSIR_REDUCTION_MIN = 2,
    TSIR_REDUCTION_MAX = 3,
    TSIR_REDUCTION_SOFTMAX_STABLE = 4
} TsirReductionOp;

typedef enum TsirCollectiveKind {
    TSIR_COLLECTIVE_SUBGROUP_ADD = 0,
    TSIR_COLLECTIVE_SUBGROUP_MIN = 1,
    TSIR_COLLECTIVE_SUBGROUP_MAX = 2,
    TSIR_COLLECTIVE_SUBGROUP_MUL = 3,
    TSIR_COLLECTIVE_SUBGROUP_BROADCAST = 4,
    TSIR_COLLECTIVE_SUBGROUP_BALLOT = 5,
    TSIR_COLLECTIVE_SUBGROUP_SHUFFLE = 6,
    TSIR_COLLECTIVE_SUBGROUP_EXCLUSIVE_SCAN = 7,
    TSIR_COLLECTIVE_SUBGROUP_INCLUSIVE_SCAN = 8,
    TSIR_COLLECTIVE_WORKGROUP_BARRIER = 9,
    TSIR_COLLECTIVE_FABRIC_REDUCE = 10,
    TSIR_COLLECTIVE_FABRIC_BROADCAST = 11,
    TSIR_COLLECTIVE_FABRIC_ALLREDUCE = 12
} TsirCollectiveKind;

typedef enum TsirReductionTreeShape {
    TSIR_TREE_BINOMIAL = 0,
    TSIR_TREE_RING = 1,
    TSIR_TREE_LINEAR = 2
} TsirReductionTreeShape;

typedef enum TsirResidencyClass {
    TSIR_RESIDENCY_PE_REPLICATED = 0,
    TSIR_RESIDENCY_PE_SLICED = 1,
    TSIR_RESIDENCY_FABRIC_STREAMED = 2,
    TSIR_RESIDENCY_HOST_COPIED = 3,
    TSIR_RESIDENCY_RECOMPUTED = 4
} TsirResidencyClass;

typedef enum TsirSemanticBodyOp {
    TSIR_BODY_UNKNOWN = 0,
    TSIR_BODY_FUSED_GEMV = 1,
    TSIR_BODY_RMS_NORM = 2,
    TSIR_BODY_GATHER = 3,
    TSIR_BODY_RESIDUAL_ADD = 4,
    TSIR_BODY_GELU_GATED = 5,
    TSIR_BODY_SILU_GATED = 6,
    TSIR_BODY_SIGMOID_GATED = 7,
    TSIR_BODY_KV_WRITE = 8,
    TSIR_BODY_KV_READ = 9,
    TSIR_BODY_ATTENTION_SCORES = 10,
    TSIR_BODY_CONV1D_DEPTHWISE = 11,
    TSIR_BODY_L2_NORMALIZE = 12,
    TSIR_BODY_LINEAR_ATTENTION = 13
} TsirSemanticBodyOp;

typedef enum TsirSemanticBindingRole {
    TSIR_ROLE_MATRIX = 0,
    TSIR_ROLE_VECTOR = 1,
    TSIR_ROLE_INPUT = 2,
    TSIR_ROLE_SCALE = 3,
    TSIR_ROLE_INDICES = 4,
    TSIR_ROLE_TABLE = 5,
    TSIR_ROLE_OUTPUT = 6,
    TSIR_ROLE_QUERY = 7,
    TSIR_ROLE_KEY = 8,
    TSIR_ROLE_VALUE = 9,
    TSIR_ROLE_KV_LEN_BUFFER = 10,
    TSIR_ROLE_PAGE_TABLE = 11,
    TSIR_ROLE_SUMMAND_A = 12,
    TSIR_ROLE_SUMMAND_B = 13,
    TSIR_ROLE_GATE = 14,
    TSIR_ROLE_KEY_PROJECTION = 15,
    TSIR_ROLE_VALUE_PROJECTION = 16,
    TSIR_ROLE_KEY_CACHE = 17,
    TSIR_ROLE_VALUE_CACHE = 18,
    TSIR_ROLE_DECODE_POSITION = 19,
    TSIR_ROLE_KEY_OUTPUT = 20,
    TSIR_ROLE_VALUE_OUTPUT = 21,
    TSIR_ROLE_WEIGHT = 22,
    TSIR_ROLE_BIAS = 23,
    TSIR_ROLE_LINEAR_STATE = 24
} TsirSemanticBindingRole;

typedef enum TsirSemanticAxisRole {
    TSIR_AXIS_OUTPUT = 0,
    TSIR_AXIS_REDUCTION = 1,
    TSIR_AXIS_HIDDEN = 2,
    TSIR_AXIS_TOKEN = 3,
    TSIR_AXIS_HEAD = 4,
    TSIR_AXIS_QUERY_SEQUENCE = 5,
    TSIR_AXIS_KEY_SEQUENCE = 6
} TsirSemanticAxisRole;

typedef enum TsirRmsNormFormula {
    TSIR_RMSNORM_SUM_SQUARES_MEAN_EPSILON_RSQRT_SCALE = 0
} TsirRmsNormFormula;

typedef enum TsirRmsNormReductionTarget {
    TSIR_RMSNORM_INTERMEDIATE_SCALAR = 0
} TsirRmsNormReductionTarget;

typedef enum TsirRmsNormEpsilonSource {
    TSIR_EPSILON_UNIFORM_FIELD = 0,
    TSIR_EPSILON_LITERAL_F32 = 1
} TsirRmsNormEpsilonSource;

typedef enum TsirSoftmaxMode {
    TSIR_SOFTMAX_TWO_PASS_STABLE = 0,
    TSIR_SOFTMAX_STREAMING_ONLINE = 1
} TsirSoftmaxMode;

typedef enum TsirCausalMode {
    TSIR_CAUSAL_NONE = 0,
    TSIR_CAUSAL_CAUSAL = 1,
    TSIR_CAUSAL_SLIDING_WINDOW = 2
} TsirCausalMode;

typedef enum TsirAttentionScaleSource {
    TSIR_SCALE_UNIFORM_FIELD = 0,
    TSIR_SCALE_LITERAL_F32 = 1
} TsirAttentionScaleSource;

typedef enum TsirLinearAttentionNormMode {
    TSIR_LINEAR_NORM_SHARED = 0,
    TSIR_LINEAR_NORM_PER_HEAD = 1
} TsirLinearAttentionNormMode;

typedef struct TsirOptU32 {
    bool has;
    uint32_t value;
} TsirOptU32;

typedef struct TsirOptU64 {
    bool has;
    uint64_t value;
} TsirOptU64;

typedef struct TsirExactness {
    TsirExactnessClass class;
    const TsirAlgorithmExactInvariant *invariants;
    size_t invariant_count;
    const char *tolerance_metric;
    double tolerance_epsilon;
    size_t tolerance_metric_length;
} TsirExactness;

typedef struct TsirRejectionEntry {
    TsirRejectionReason reason;
    const char *node_path;
    const char *detail;
    size_t node_path_length;
    size_t detail_length;
} TsirRejectionEntry;

typedef struct TsirNumericalContract {
    TsirScalarKind accumulation;
    TsirReductionAssociativity associativity;
    TsirNanInfPolicy nan_inf;
} TsirNumericalContract;

typedef struct TsirCollectiveSemanticNode {
    TsirCollectiveKind kind;
    int32_t axis;
    TsirExactness exactness;
    TsirScalarKind dtype;
} TsirCollectiveSemanticNode;

typedef struct TsirCollectiveRealizationNode {
    uint32_t semantic_index;
    TsirReductionTreeShape tree_shape;
    TsirOptU32 fabric_color;
    uint32_t group_size;
} TsirCollectiveRealizationNode;

typedef struct TsirReductionRealizationNode {
    uint32_t semantic_index;
    TsirReductionTreeShape tree_shape;
} TsirReductionRealizationNode;

typedef struct TsirBufferBinding {
    const char *name;
    uint32_t group;
    uint32_t binding;
    const uint64_t *logical_shape;
    size_t logical_shape_len;
    TsirScalarKind elem;
    bool read_write;
    size_t name_length;
} TsirBufferBinding;

typedef struct TsirIterationAxis {
    const char *name;
    const char *lower_bound;
    const char *upper_bound;
    const char *step;
    size_t name_length;
    size_t lower_bound_length;
    size_t upper_bound_length;
    size_t step_length;
} TsirIterationAxis;

typedef struct TsirReductionRegion {
    uint32_t axis;
    TsirReductionOp op;
    TsirNumericalContract contract;
    uint32_t target_binding;
} TsirReductionRegion;

typedef struct TsirSemanticBodyBinding {
    uint32_t binding_index;
    TsirSemanticBindingRole role;
} TsirSemanticBodyBinding;

typedef struct TsirSemanticBodyAxis {
    uint32_t axis_index;
    TsirSemanticAxisRole role;
} TsirSemanticBodyAxis;

typedef struct TsirRmsNormEpsilon {
    TsirRmsNormEpsilonSource source;
    const char *path;
    TsirOptU32 binding_index;
    TsirOptU32 byte_offset;
    bool has_literal_f64;
    double literal_f64;
    size_t path_length;
} TsirRmsNormEpsilon;

typedef struct TsirRmsNormBody {
    TsirRmsNormFormula formula;
    TsirRmsNormEpsilon epsilon;
    uint32_t hidden_extent_axis;
    TsirRmsNormReductionTarget reduction_target;
} TsirRmsNormBody;

typedef struct TsirAttentionScoresBody {
    TsirSoftmaxMode softmax_mode;
    uint32_t head_dim;
    uint32_t key_sequence_axis;
    TsirAttentionScaleSource scale_source;
    TsirOptU32 scale_binding_index;
    TsirOptU32 scale_byte_offset;
    bool has_scale_literal_f64;
    double scale_literal_f64;
    bool has_softcap;
    TsirCausalMode causal_mode;
    TsirOptU32 sliding_window_size;
    TsirOptU32 query_seq_len;
} TsirAttentionScoresBody;

typedef struct TsirConv1DDepthwiseBody {
    uint32_t channels;
    uint32_t kernel_size;
    bool has_bias;
} TsirConv1DDepthwiseBody;

typedef struct TsirL2NormalizeBody {
    uint32_t hidden;
    float eps;
} TsirL2NormalizeBody;

typedef struct TsirLinearAttentionBody {
    uint32_t key_dim;
    uint32_t value_dim;
    uint32_t key_heads;
    uint32_t value_heads;
    TsirLinearAttentionNormMode norm_mode;
    bool has_dt_bias;
} TsirLinearAttentionBody;

typedef struct TsirSemanticBody {
    TsirSemanticBodyOp op;
    const TsirSemanticBodyBinding *binding_roles;
    size_t binding_role_count;
    const TsirSemanticBodyAxis *axis_roles;
    size_t axis_role_count;
    bool has_rms_norm;
    TsirRmsNormBody rms_norm;
    bool has_attention_scores;
    TsirAttentionScoresBody attention_scores;
    bool has_conv1d_depthwise;
    TsirConv1DDepthwiseBody conv1d_depthwise;
    bool has_l2_normalize;
    TsirL2NormalizeBody l2_normalize;
    bool has_linear_attention;
    TsirLinearAttentionBody linear_attention;
} TsirSemanticBody;

typedef struct TsirSemanticFunction {
    const char *name;
    TsirKernelFamilyHint family_hint;
    const TsirIterationAxis *axes;
    size_t axis_count;
    const TsirBufferBinding *bindings;
    size_t binding_count;
    const TsirReductionRegion *reductions;
    size_t reduction_count;
    const TsirCollectiveSemanticNode *collectives;
    size_t collective_count;
    TsirSemanticBody body;
    uint8_t source_digest[32];
    size_t name_length;
} TsirSemanticFunction;

typedef struct TsirSemantic {
    uint32_t contract_version;
    const char *frontend_version;
    const TsirSemanticFunction *functions;
    size_t function_count;
    const TsirRejectionEntry *rejections;
    size_t rejection_count;
    size_t frontend_version_length;
} TsirSemantic;

typedef struct TsirResidencyDecision {
    uint32_t binding_index;
    TsirResidencyClass class;
    TsirOptU32 axis;
    TsirOptU32 shards;
    TsirOptU32 fabric_color;
    TsirOptU64 chunk_bytes;
} TsirResidencyDecision;

typedef struct TsirTileFactors {
    const uint32_t *per_axis;
    size_t per_axis_count;
} TsirTileFactors;

typedef struct TsirPEGridShape {
    uint32_t width;
    uint32_t height;
} TsirPEGridShape;

typedef struct TsirRealizationFunction {
    uint32_t semantic_index;
    TsirTileFactors tiles;
    TsirPEGridShape pe_grid;
    const TsirResidencyDecision *residency;
    size_t residency_count;
    const TsirCollectiveRealizationNode *collectives;
    size_t collective_count;
    const TsirReductionRealizationNode *reductions;
    size_t reduction_count;
    const char *emitter_params_json;
    uint8_t target_descriptor_hash[32];
    size_t emitter_params_json_length;
} TsirRealizationFunction;

typedef struct TsirRealization {
    uint32_t contract_version;
    const TsirRealizationFunction *functions;
    size_t function_count;
    uint8_t emitter_digest[32];
    const TsirRejectionEntry *rejections;
    size_t rejection_count;
} TsirRealization;

typedef struct TsirDigests {
    uint8_t semantic[32];
    uint8_t realization[32];
    uint8_t emitter[32];
} TsirDigests;

typedef struct TsirManifestLoweringEntry {
    const char *kernel_ref;
    const char *backend;
    uint8_t target_descriptor_correctness_hash[32];
    const char *frontend_version;
    uint8_t tsir_semantic_digest[32];
    uint8_t tsir_realization_digest[32];
    uint8_t emitter_digest[32];
    const char *compiler_version;
    TsirExactness exactness;
    const TsirRejectionReason *rejection_reasons;
    size_t rejection_reason_count;
    size_t kernel_ref_length;
    size_t backend_length;
    size_t frontend_version_length;
    size_t compiler_version_length;
} TsirManifestLoweringEntry;

const char *tsir_exactness_class_name(TsirExactnessClass value);
const char *tsir_invariant_name(TsirAlgorithmExactInvariant value);
const char *tsir_rejection_reason_name(TsirRejectionReason value);
const char *tsir_family_hint_name(TsirKernelFamilyHint value);
const char *tsir_scalar_name(TsirScalarKind value);
const char *tsir_associativity_name(TsirReductionAssociativity value);
const char *tsir_nan_inf_name(TsirNanInfPolicy value);
const char *tsir_reduction_op_name(TsirReductionOp value);
const char *tsir_collective_kind_name(TsirCollectiveKind value);
const char *tsir_tree_shape_name(TsirReductionTreeShape value);
const char *tsir_residency_class_name(TsirResidencyClass value);
const char *tsir_body_op_name(TsirSemanticBodyOp value);
const char *tsir_binding_role_name(TsirSemanticBindingRole value);
const char *tsir_axis_role_name(TsirSemanticAxisRole value);
const char *tsir_rms_norm_formula_name(TsirRmsNormFormula value);
const char *tsir_rms_norm_target_name(TsirRmsNormReductionTarget value);
const char *tsir_epsilon_source_name(TsirRmsNormEpsilonSource value);
const char *tsir_softmax_mode_name(TsirSoftmaxMode value);
const char *tsir_causal_mode_name(TsirCausalMode value);
const char *tsir_scale_source_name(TsirAttentionScaleSource value);
const char *tsir_linear_norm_mode_name(TsirLinearAttentionNormMode value);

uint8_t tsir_scalar_byte_size(TsirScalarKind kind);

/* Cross-field well-formedness of an Exactness value, following the
 * schema comments: invariants only for algorithm_exact, metric/epsilon
 * only for tolerance_bounded (metric required and non-empty there,
 * epsilon must be finite; epsilon must be exactly zero elsewhere). */
bool tsir_exactness_is_well_formed(const TsirExactness *exactness);

/* Growable byte buffer. Once err is set, further appends are no-ops
 * and err is preserved, so the first failure is never overwritten.
 * The caller frees data with free(); tsir_buf_free does that and
 * resets the struct. */
typedef struct TsirBuffer {
    char *data;
    size_t len;
    size_t cap;
    TsirStatus err;
} TsirBuffer;

void tsir_buf_init(TsirBuffer *buf);
void tsir_buf_free(TsirBuffer *buf);
void tsir_buf_putc(TsirBuffer *buf, char c);
void tsir_buf_putn(TsirBuffer *buf, const char *text, size_t len);
void tsir_buf_puts(TsirBuffer *buf, const char *text);

#endif
