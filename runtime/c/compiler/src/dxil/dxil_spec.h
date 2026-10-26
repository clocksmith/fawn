/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_SPEC_H
#define DOE_DXIL_SPEC_H

#include <stdint.h>

/* DXIL specification constants. Faithful port of dxil_spec.zig: every
 * numeric value below is identical to the Zig constant of the same name.
 * FourCC values and magic bytes are exposed through small inline accessors
 * (byte order on the wire is the order the accessor writes). */

/* LLVM bitcode stream encoding constants. */
#define DXIL_BITCODE_MAGIC_U32 0x4243C0DEu

static inline void dxil_llvm_magic_bytes(uint8_t out[4]) {
    out[0] = (uint8_t)'B';
    out[1] = (uint8_t)'C';
    out[2] = 0xC0u;
    out[3] = 0xDEu;
}

typedef enum DxilBlockId {
    DXIL_BLOCK_MODULE = 8,
    DXIL_BLOCK_PARAMATTR = 9,
    DXIL_BLOCK_PARAMATTR_GROUP = 10,
    DXIL_BLOCK_CONSTANTS = 11,
    DXIL_BLOCK_FUNCTION = 12,
    DXIL_BLOCK_VALUE_SYMTAB = 14,
    DXIL_BLOCK_METADATA = 15,
    DXIL_BLOCK_METADATA_ATTACHMENT = 16,
    DXIL_BLOCK_TYPE = 17,
    DXIL_BLOCK_STRTAB = 23
} DxilBlockId;

typedef enum DxilAbbrevId {
    DXIL_ABBREV_END_BLOCK = 0,
    DXIL_ABBREV_ENTER_SUBBLOCK = 1,
    DXIL_ABBREV_DEFINE_ABBREV = 2,
    DXIL_ABBREV_UNABBREV_RECORD = 3
} DxilAbbrevId;

typedef enum DxilModuleCode {
    DXIL_MODULE_VERSION = 1,
    DXIL_MODULE_TRIPLE = 2,
    DXIL_MODULE_DATALAYOUT = 3,
    DXIL_MODULE_GLOBALVAR = 7,
    DXIL_MODULE_FUNCTION = 8
} DxilModuleCode;

typedef enum DxilTypeCode {
    DXIL_TYPE_NUMENTRY = 1,
    DXIL_TYPE_VOID = 2,
    DXIL_TYPE_FLOAT = 3,
    DXIL_TYPE_DOUBLE = 4,
    DXIL_TYPE_LABEL = 5,
    DXIL_TYPE_INTEGER = 7,
    DXIL_TYPE_POINTER = 8,
    DXIL_TYPE_HALF = 10,
    DXIL_TYPE_ARRAY = 11,
    DXIL_TYPE_VECTOR = 12,
    DXIL_TYPE_METADATA = 16,
    DXIL_TYPE_STRUCT_ANON = 18,
    DXIL_TYPE_STRUCT_NAMED = 20,
    DXIL_TYPE_FUNCTION = 21
} DxilTypeCode;

typedef enum DxilConstantCode {
    DXIL_CONSTANT_SETTYPE = 1,
    DXIL_CONSTANT_NULL = 2,
    DXIL_CONSTANT_UNDEF = 3,
    DXIL_CONSTANT_INTEGER = 4,
    DXIL_CONSTANT_FLOAT = 6,
    DXIL_CONSTANT_AGGREGATE = 7,
    DXIL_CONSTANT_STRING = 8,
    DXIL_CONSTANT_CE_GEP = 12
} DxilConstantCode;

typedef enum DxilFunctionCode {
    DXIL_FUNCTION_DECLAREBLOCKS = 1,
    DXIL_FUNCTION_INST_BINOP = 2,
    DXIL_FUNCTION_INST_CAST = 3,
    DXIL_FUNCTION_INST_RET = 10,
    DXIL_FUNCTION_INST_BR = 11,
    DXIL_FUNCTION_INST_SWITCH = 12,
    DXIL_FUNCTION_INST_PHI = 16,
    DXIL_FUNCTION_INST_ALLOCA = 19,
    DXIL_FUNCTION_INST_LOAD = 20,
    DXIL_FUNCTION_INST_STORE = 24,
    DXIL_FUNCTION_INST_EXTRACTVAL = 26,
    DXIL_FUNCTION_INST_INSERTVAL = 27,
    DXIL_FUNCTION_INST_CMP = 28,
    DXIL_FUNCTION_INST_CALL = 34,
    DXIL_FUNCTION_INST_GEP = 43
} DxilFunctionCode;

typedef enum DxilMetadataCode {
    DXIL_METADATA_STRING = 1,
    DXIL_METADATA_VALUE = 2,
    DXIL_METADATA_NODE = 3,
    DXIL_METADATA_NAME = 4,
    DXIL_METADATA_KIND = 6,
    DXIL_METADATA_NAMED_NODE = 10
} DxilMetadataCode;

typedef enum DxilValueSymtabCode {
    DXIL_SYMTAB_ENTRY = 1,
    DXIL_SYMTAB_BBENTRY = 2
} DxilValueSymtabCode;

/* LLVM binary operations. Integer and float opcodes share values 0..4. */
typedef enum DxilBinOp {
    DXIL_BINOP_ADD = 0,
    DXIL_BINOP_SUB = 1,
    DXIL_BINOP_MUL = 2,
    DXIL_BINOP_UDIV = 3,
    DXIL_BINOP_SDIV = 4,
    DXIL_BINOP_UREM = 5,
    DXIL_BINOP_SREM = 6,
    DXIL_BINOP_SHL = 7,
    DXIL_BINOP_LSHR = 8,
    DXIL_BINOP_ASHR = 9,
    DXIL_BINOP_AND = 10,
    DXIL_BINOP_OR = 11,
    DXIL_BINOP_XOR = 12,
    DXIL_BINOP_FADD = 0,
    DXIL_BINOP_FSUB = 1,
    DXIL_BINOP_FMUL = 2,
    DXIL_BINOP_FDIV = 3,
    DXIL_BINOP_FREM = 4
} DxilBinOp;

typedef enum DxilCastOp {
    DXIL_CAST_TRUNC = 0,
    DXIL_CAST_ZEXT = 1,
    DXIL_CAST_SEXT = 2,
    DXIL_CAST_FPTOUI = 3,
    DXIL_CAST_FPTOSI = 4,
    DXIL_CAST_UITOFP = 5,
    DXIL_CAST_SITOFP = 6,
    DXIL_CAST_FPTRUNC = 7,
    DXIL_CAST_FPEXT = 8,
    DXIL_CAST_BITCAST = 11
} DxilCastOp;

typedef enum DxilCmpPred {
    DXIL_CMP_ICMP_EQ = 32,
    DXIL_CMP_ICMP_NE = 33,
    DXIL_CMP_ICMP_UGT = 34,
    DXIL_CMP_ICMP_UGE = 35,
    DXIL_CMP_ICMP_ULT = 36,
    DXIL_CMP_ICMP_ULE = 37,
    DXIL_CMP_ICMP_SGT = 38,
    DXIL_CMP_ICMP_SGE = 39,
    DXIL_CMP_ICMP_SLT = 40,
    DXIL_CMP_ICMP_SLE = 41,
    DXIL_CMP_FCMP_OEQ = 1,
    DXIL_CMP_FCMP_OGT = 2,
    DXIL_CMP_FCMP_OGE = 3,
    DXIL_CMP_FCMP_OLT = 4,
    DXIL_CMP_FCMP_OLE = 5,
    DXIL_CMP_FCMP_ONE = 6,
    DXIL_CMP_FCMP_ORD = 7,
    DXIL_CMP_FCMP_UNO = 8
} DxilCmpPred;

/* DXIL opcodes (dx.op intrinsic call arg0). */
typedef enum DxilOpcode {
    DXIL_OP_LOAD_INPUT = 4,
    DXIL_OP_STORE_OUTPUT = 5,
    DXIL_OP_DOT2 = 54,
    DXIL_OP_DOT3 = 55,
    DXIL_OP_DOT4 = 56,
    DXIL_OP_CREATE_HANDLE = 57,
    DXIL_OP_CBUFFER_LOAD_LEGACY = 59,
    DXIL_OP_SAMPLE = 60,
    DXIL_OP_SAMPLE_LEVEL = 62,
    DXIL_OP_SAMPLE_CMP = 64,
    DXIL_OP_TEXTURE_LOAD = 66,
    DXIL_OP_TEXTURE_STORE = 67,
    DXIL_OP_BUFFER_LOAD = 68,
    DXIL_OP_BUFFER_STORE = 69,
    DXIL_OP_BUFFER_UPDATE_COUNTER = 70,
    DXIL_OP_GET_DIMENSIONS = 72,
    DXIL_OP_TEXTURE_GATHER = 73,
    DXIL_OP_ATOMIC_BINOP = 78,
    DXIL_OP_ATOMIC_CMP_XCHG = 79,
    DXIL_OP_BARRIER = 80,
    DXIL_OP_THREAD_ID = 93,
    DXIL_OP_GROUP_ID = 94,
    DXIL_OP_THREAD_ID_IN_GROUP = 95,
    DXIL_OP_FLATTENED_THREAD_ID_IN_GROUP = 96,
    DXIL_OP_EMIT_STREAM = 97,
    DXIL_OP_CUT_STREAM = 98,
    DXIL_OP_WAVE_IS_FIRST_LANE = 110,
    DXIL_OP_WAVE_GET_LANE_INDEX = 111,
    DXIL_OP_WAVE_GET_LANE_COUNT = 112,
    DXIL_OP_WAVE_ANY_TRUE = 113,
    DXIL_OP_WAVE_ALL_TRUE = 114,
    DXIL_OP_WAVE_ACTIVE_BALLOT = 116,
    DXIL_OP_WAVE_READ_LANE_AT = 117,
    DXIL_OP_WAVE_ACTIVE_OP = 119,
    DXIL_OP_WAVE_ACTIVE_BIT = 120,
    DXIL_OP_WAVE_PREFIX_OP = 121,
    DXIL_OP_BUFFER_LOAD_RAW = 139,
    DXIL_OP_BUFFER_STORE_RAW = 140
} DxilOpcode;

typedef enum DxilWaveOp {
    DXIL_WAVE_SUM = 0,
    DXIL_WAVE_PRODUCT = 1,
    DXIL_WAVE_MIN = 2,
    DXIL_WAVE_MAX = 3
} DxilWaveOp;

typedef enum DxilWaveBitOp {
    DXIL_WAVE_BIT_AND = 0,
    DXIL_WAVE_BIT_OR = 1,
    DXIL_WAVE_BIT_XOR = 2
} DxilWaveBitOp;

typedef enum DxilBarrierMode {
    DXIL_BARRIER_SYNC_THREAD_GROUP = 1,
    DXIL_BARRIER_UAV_FENCE_GLOBAL = 2,
    DXIL_BARRIER_UAV_FENCE_THREAD_GROUP = 4,
    DXIL_BARRIER_TGSM_FENCE = 8
} DxilBarrierMode;

/* DXBC container format constants. */
static inline void dxil_dxbc_fourcc_bytes(uint8_t out[4]) {
    out[0] = (uint8_t)'D';
    out[1] = (uint8_t)'X';
    out[2] = (uint8_t)'B';
    out[3] = (uint8_t)'C';
}

#define DXIL_DXBC_HEADER_SIZE 32u
#define DXIL_DXBC_HASH_SIZE 16u

typedef enum DxilPartFourcc {
    DXIL_PART_FOURCC_DXIL = 0,
    DXIL_PART_FOURCC_ISGN = 1,
    DXIL_PART_FOURCC_OSGN = 2,
    DXIL_PART_FOURCC_PSV0 = 3,
    DXIL_PART_FOURCC_RDEF = 4,
    DXIL_PART_FOURCC_SHDR = 5,
    DXIL_PART_FOURCC_SFI0 = 6,
    DXIL_PART_FOURCC_HASH = 7
} DxilPartFourcc;

/* Writes the four bytes of a part FourCC, in wire order. */
void dxil_part_fourcc_bytes(DxilPartFourcc fourcc, uint8_t out[4]);

/* DXIL program header values. */
#define DXIL_PROGRAM_HEADER_SIZE 24u
#define DXIL_PROGRAM_MINOR_VERSION 0u
#define DXIL_PROGRAM_MAJOR_VERSION 1u

typedef enum DxilShaderKind {
    DXIL_SHADER_PIXEL = 0,
    DXIL_SHADER_VERTEX = 1,
    DXIL_SHADER_GEOMETRY = 2,
    DXIL_SHADER_HULL = 3,
    DXIL_SHADER_DOMAIN = 4,
    DXIL_SHADER_COMPUTE = 5
} DxilShaderKind;

/* Shader model target triple components. */
#define DXIL_TARGET_DATALAYOUT \
    "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
#define DXIL_TARGET_TRIPLE_CS "dxil-ms-dx"

typedef enum DxilMdTag {
    DXIL_MD_SHADER_MODEL = 0,
    DXIL_MD_RESOURCES = 1,
    DXIL_MD_ENTRY_POINTS = 2,
    DXIL_MD_SHADER_FLAGS = 3,
    DXIL_MD_NUM_THREADS = 4,
    DXIL_MD_SIGNATURE_ELEMENT = 5,
    DXIL_MD_INPUT_SIG = 6,
    DXIL_MD_OUTPUT_SIG = 7
} DxilMdTag;

typedef enum DxilResourceClass {
    DXIL_RESOURCE_SRV = 0,
    DXIL_RESOURCE_UAV = 1,
    DXIL_RESOURCE_CBV = 2,
    DXIL_RESOURCE_SAMPLER = 3
} DxilResourceClass;

typedef enum DxilResourceKind {
    DXIL_RESOURCE_INVALID = 0,
    DXIL_RESOURCE_TEXTURE_1D = 1,
    DXIL_RESOURCE_TEXTURE_2D = 2,
    DXIL_RESOURCE_TEXTURE_2D_MS = 3,
    DXIL_RESOURCE_TEXTURE_3D = 4,
    DXIL_RESOURCE_TEXTURE_CUBE = 5,
    DXIL_RESOURCE_TYPED_BUFFER = 10,
    DXIL_RESOURCE_RAW_BUFFER = 11,
    DXIL_RESOURCE_STRUCTURED_BUFFER = 12,
    DXIL_RESOURCE_CBUFFER = 13,
    DXIL_RESOURCE_KIND_SAMPLER = 14,
    DXIL_RESOURCE_TBUFFER = 15
} DxilResourceKind;

typedef enum DxilComponentType {
    DXIL_COMPONENT_INVALID = 0,
    DXIL_COMPONENT_U32 = 1,
    DXIL_COMPONENT_I32 = 2,
    DXIL_COMPONENT_F32 = 3,
    DXIL_COMPONENT_U16 = 4,
    DXIL_COMPONENT_I16 = 5,
    DXIL_COMPONENT_F16 = 6,
    DXIL_COMPONENT_U64 = 7,
    DXIL_COMPONENT_I64 = 8,
    DXIL_COMPONENT_F64 = 9
} DxilComponentType;

typedef enum DxilSemanticKind {
    DXIL_SEMANTIC_ARBITRARY = 0,
    DXIL_SEMANTIC_VERTEX_ID = 1,
    DXIL_SEMANTIC_INSTANCE_ID = 2,
    DXIL_SEMANTIC_POSITION = 3,
    DXIL_SEMANTIC_RENDER_TARGET_ARRAY_INDEX = 4,
    DXIL_SEMANTIC_VIEWPORT_ARRAY_INDEX = 5,
    DXIL_SEMANTIC_CLIP_DISTANCE = 6,
    DXIL_SEMANTIC_CULL_DISTANCE = 7,
    DXIL_SEMANTIC_TARGET = 8,
    DXIL_SEMANTIC_DEPTH = 9,
    DXIL_SEMANTIC_COVERAGE = 10,
    DXIL_SEMANTIC_DISPATCH_THREAD_ID = 12,
    DXIL_SEMANTIC_GROUP_ID = 13,
    DXIL_SEMANTIC_GROUP_THREAD_ID = 14,
    DXIL_SEMANTIC_GROUP_INDEX = 15,
    DXIL_SEMANTIC_IS_FRONT_FACE = 16,
    DXIL_SEMANTIC_SAMPLE_INDEX = 17,
    DXIL_SEMANTIC_PRIMITIVE_ID = 18
} DxilSemanticKind;

typedef enum DxilLinkage {
    DXIL_LINKAGE_EXTERNAL = 0,
    DXIL_LINKAGE_INTERNAL = 8
} DxilLinkage;

#endif /* DOE_DXIL_SPEC_H */
