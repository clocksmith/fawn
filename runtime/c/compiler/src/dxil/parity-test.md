# DXIL behavioral parity test definition

Written FIRST, before any C code. Source of truth: the Zig sources at
`~/workspace/fawn-tree`, head `43040741d1d4822d64c8ec8976d6c652ec345242`
(read-only; nothing in that tree is modified by this work).

Verified source inventory (by `wc -l`, this run):

| Zig file | Lines |
| --- | --- |
| `runtime/zig/src/compiler/wgsl/emit/dxil/dxil_spec.zig` | 343 |
| `runtime/zig/src/compiler/wgsl/emit/dxil/dxil_bitcode.zig` | 211 |
| `runtime/zig/src/compiler/wgsl/emit/dxil/dxil_builder.zig` | 466 |
| `runtime/zig/src/compiler/wgsl/emit/dxil/dxil_container.zig` | 204 |
| `runtime/zig/src/compiler/wgsl/emit/dxil/dxil_serialize.zig` | 360 |
| `runtime/zig/src/compiler/wgsl/emit/dxil/dxil_validate.zig` | 287 |
| `runtime/zig/src/compiler/wgsl/emit/dxil/emit_dxil.zig` | 312 |
| `runtime/zig/src/compiler/wgsl/emit/dxil/emit_dxil_native.zig` | 582 |
| `runtime/zig/src/compiler/wgsl/pipeline/translate_dxil.zig` | 74 |
| Total, 9 files | 2839 |

No Zig toolchain exists on this VM, so Zig reference bytes cannot be
generated here. Byte expectations below are derived by hand from the Zig
semantics, and the MD5 expectations are cross-checked with Python
`hashlib` (an independent implementation of the same RFC 1321 algorithm
the Zig code calls via `std.crypto.hash.Md5`). Where the Zig code and its
own comment disagree, the CODE wins and the disagreement is recorded.

Conventions for the C port (from `runtime/c/STYLE.md`):

- C11, four-space indentation, at most 999 physical lines per file,
  `SPDX-License-Identifier: MIT` within the first three lines, header
  guards, `-std=c11 -Wall -Wextra -Wpedantic -Werror`.
- Every function takes a caller-owned output buffer and length, returns a
  status, and on error leaves the first error as the reported one.
  Unsupported behavior fails visibly; nothing is a silent no-op.
- Zig panics (out-of-bounds indexing, integer-cast traps) are NOT copied:
  the C port returns `DXIL_INVALID_IR` or `DXIL_OUTPUT_TOO_LARGE` at the
  same boundary instead. Each such divergence is listed in `gaps.md`.

## 1. dxil_spec.zig -> dxil_spec.h (constants only)

For every constant group, the C header must carry the identical numeric
value. Tests assert a sample from each group, not just that the header
compiles:

- Bitcode magic: bytes `42 43 C0 DE` (`'B','C',0xC0,0xDE`), u32
  `0x4243C0DE`.
- Block IDs: MODULE 8, PARAMATTR 9, PARAMATTR_GROUP 10, CONSTANTS 11,
  FUNCTION 12, VALUE_SYMTAB 14, METADATA 15, METADATA_ATTACHMENT 16,
  TYPE 17, STRTAB 23.
- Abbrev IDs: END_BLOCK 0, ENTER_SUBBLOCK 1, DEFINE_ABBREV 2,
  UNABBREV_RECORD 3.
- Module codes: VERSION 1, TRIPLE 2, DATALAYOUT 3, GLOBALVAR 7,
  FUNCTION 8. Type codes: NUMENTRY 1, VOID 2, FLOAT 3, DOUBLE 4, LABEL 5,
  INTEGER 7, POINTER 8, HALF 10, ARRAY 11, VECTOR 12, METADATA 16,
  STRUCT_ANON 18, STRUCT_NAMED 20, FUNCTION 21.
- Constant codes: SETTYPE 1, NULL 2, UNDEF 3, INTEGER 4, FLOAT 6,
  AGGREGATE 7, STRING 8, CE_GEP 12.
- Function codes: DECLAREBLOCKS 1, INST_BINOP 2, INST_CAST 3,
  INST_RET 10, INST_BR 11, INST_SWITCH 12, INST_PHI 16, INST_ALLOCA 19,
  INST_LOAD 20, INST_STORE 24, INST_EXTRACTVAL 26, INST_INSERTVAL 27,
  INST_CMP 28, INST_CALL 34, INST_GEP 43.
- Metadata codes: STRING 1, VALUE 2, NODE 3, NAME 4, KIND 6,
  NAMED_NODE 10. Value-symtab codes: ENTRY 1, BBENTRY 2.
- BinOps: ADD 0, SUB 1, MUL 2, UDIV 3, SDIV 4, UREM 5, SREM 6, SHL 7,
  LSHR 8, ASHR 9, AND 10, OR 11, XOR 12; float FADD 0, FSUB 1, FMUL 2,
  FDIV 3, FREM 4.
- CastOps: TRUNC 0, ZEXT 1, SEXT 2, FPTOUI 3, FPTOSI 4, UITOFP 5,
  SITOFP 6, FPTRUNC 7, FPEXT 8, BITCAST 11.
- CmpPred: ICMP_EQ 32, ICMP_NE 33, ICMP_UGT 34, ICMP_UGE 35,
  ICMP_ULT 36, ICMP_ULE 37, ICMP_SGT 38, ICMP_SGE 39, ICMP_SLT 40,
  ICMP_SLE 41; FCMP_OEQ 1, FCMP_OGT 2, FCMP_OGE 3, FCMP_OLT 4,
  FCMP_OLE 5, FCMP_ONE 6, FCMP_ORD 7, FCMP_UNO 8.
- DXIL opcodes (spot-checked in full in the mapping tests): THREAD_ID 93,
  GROUP_ID 94, THREAD_ID_IN_GROUP 95, FLATTENED_THREAD_ID_IN_GROUP 96,
  CREATE_HANDLE 57, CBUFFER_LOAD_LEGACY 59, SAMPLE 60, SAMPLE_LEVEL 62,
  SAMPLE_CMP 64, TEXTURE_LOAD 66, TEXTURE_STORE 67, BUFFER_LOAD 68,
  BUFFER_STORE 69, BUFFER_UPDATE_COUNTER 70, GET_DIMENSIONS 72,
  TEXTURE_GATHER 73, ATOMIC_BINOP 78, ATOMIC_CMP_XCHG 79, BARRIER 80,
  LOAD_INPUT 4, STORE_OUTPUT 5, EMIT_STREAM 97, CUT_STREAM 98,
  DOT2 54, DOT3 55, DOT4 56, WAVE_IS_FIRST_LANE 110,
  WAVE_GET_LANE_INDEX 111, WAVE_GET_LANE_COUNT 112, WAVE_ANY_TRUE 113,
  WAVE_ALL_TRUE 114, WAVE_ACTIVE_BALLOT 116, WAVE_READ_LANE_AT 117,
  WAVE_ACTIVE_OP 119, WAVE_ACTIVE_BIT 120, WAVE_PREFIX_OP 121,
  BUFFER_LOAD_RAW 139, BUFFER_STORE_RAW 140.
- WaveOp: SUM 0, PRODUCT 1, MIN 2, MAX 3. WaveBitOp: AND 0, OR 1, XOR 2.
- BarrierMode: SYNC_THREAD_GROUP 1, UAV_FENCE_GLOBAL 2,
  UAV_FENCE_THREAD_GROUP 4, TGSM_FENCE 8.
- DXBC: FOURCC `DXBC`, header size 32, hash size 16. Part FOURCCs:
  DXIL, ISGN, OSGN, PSV0, RDEF, SHDR, SFI0, HASH.
- DXIL program header size 24, DXIL version major 1 minor 0.
- ShaderKind: PIXEL 0, VERTEX 1, GEOMETRY 2, HULL 3, DOMAIN 4,
  COMPUTE 5.
- Target triple `dxil-ms-dx`; datalayout string exactly
  `e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64`.
- DxilMdTag: SHADER_MODEL 0, RESOURCES 1, ENTRY_POINTS 2, SHADER_FLAGS 3,
  NUM_THREADS 4, SIGNATURE_ELEMENT 5, INPUT_SIG 6, OUTPUT_SIG 7.
- ResourceClass: SRV 0, UAV 1, CBV 2, SAMPLER 3.
- ResourceKind: INVALID 0, TEXTURE_1D 1, TEXTURE_2D 2, TEXTURE_2D_MS 3,
  TEXTURE_3D 4, TEXTURE_CUBE 5, TYPED_BUFFER 10, RAW_BUFFER 11,
  STRUCTURED_BUFFER 12, CBUFFER 13, SAMPLER 14, TBUFFER 15.
- ComponentType: INVALID 0, U32 1, I32 2, F32 3, U16 4, I16 5, F16 6,
  U64 7, I64 8, F64 9.
- SemanticKind: ARBITRARY 0, VERTEX_ID 1, INSTANCE_ID 2, POSITION 3,
  RENDER_TARGET_ARRAY_INDEX 4, VIEWPORT_ARRAY_INDEX 5, CLIP_DISTANCE 6,
  CULL_DISTANCE 7, TARGET 8, DEPTH 9, COVERAGE 10, DISPATCH_THREAD_ID 12,
  GROUP_ID 13, GROUP_THREAD_ID 14, GROUP_INDEX 15, IS_FRONT_FACE 16,
  SAMPLE_INDEX 17, PRIMITIVE_ID 18.
- Linkage: EXTERNAL 0, INTERNAL 8.
- EmitError set: OutOfMemory, OutputTooLarge, UnsupportedConstruct,
  InvalidIr, UnsupportedBuiltin.

## 2. dxil_bitcode.zig -> dxil_bitcode.h/.c

Observable behaviors (Zig `Writer`, fixed caller buffer, no heap):

- `emit_raw_bytes` first flushes to a byte boundary, then copies; too
  small a buffer gives OutputTooLarge and no partial copy past the end.
- `emit_bits(value, n)`: little-endian bit order within bytes (low bits
  first), existing bits outside the written field are preserved (the Zig
  code masks and ORs), crossing a byte boundary advances correctly.
  Writing past the buffer end gives OutputTooLarge.
- `emit_vbr(value, width)`: width below 2 gives InvalidIr. Data bits are
  `width - 1` per chunk, low chunk first, continuation bit set on every
  chunk except the last. Examples at width 4 (3 data bits): 0 encodes as
  one 4-bit chunk `0000`; 5 encodes as one chunk `1101` (value bits 101
  plus no continuation... precisely: chunk value 5, no continuation bit,
  4 bits `0101`); 0x1234 encodes as multiple chunks and takes more bytes
  than 5 (the Zig test asserts position growth only). The C test asserts
  exact chunk bits for 0, 5, 7, 8 at width 4 by decoding the bitstream.
- `emit_signed_vbr`: sign rotated to LSB: non-negative `v` encodes as
  `v << 1`; negative `v` encodes as `((-v - 1) << 1) | 1`. So 0 -> 0,
  1 -> 2, -1 -> 1, -2 -> 3, 2 -> 4.
- `enter_block(id, new_width)`: emits ENTER_SUBBLOCK (abbrev id 1) in
  the current abbrev width, then VBR block id at width 8, then VBR new
  width at width 4, then aligns to a 32-bit boundary with zero padding,
  then reserves a 4-byte block-length word (zeroed). Nesting depth is
  capped at 16: the 17th nested enter gives OutputTooLarge. The current
  abbrev width becomes the new width inside the block.
- `exit_block`: emits END_BLOCK in the current width, aligns to 32 bits,
  then patches the reserved length word with the word count of the block
  content (bytes after the length word, rounded up to a multiple of 4,
  divided by 4), little-endian, and restores the previous abbrev width.
  Exiting with depth 0 gives InvalidIr (after the END_BLOCK bits and
  alignment have already been written, mirroring Zig order).
- `emit_record(code, operands)`: UNABBREV_RECORD (id 3) in current
  width, then VBR code at width 6, VBR operand count at width 6, then
  each operand as VBR at width 6.
- `emit_record_with_string(code, prefix, text)`: operand count is
  `prefix.len + text.len`; prefix operands then one VBR per text byte
  (the byte value itself, at width 6, so bytes >= 64 take multiple
  chunks). `emit_string_record` is the same with an empty prefix.
- `flush_to_byte_boundary` / `finalize`: a partial byte is closed by
  advancing one byte (bits above the partial position are left as they
  were in the buffer; callers zero the buffer first, as the Zig tests
  do); `finalize` returns the byte position.
- Zig test parity: magic + MODULE block (width 3) + VERSION record {1}
  yields size > 4 and the first four bytes equal the LLVM magic.
- Zig quirk preserved and documented: `align_to_32` silently skips the
  zero padding when the buffer has no room for it, and only the
  subsequent length-word reservation (or a later write) reports
  OutputTooLarge.

## 3. dxil_builder.zig -> dxil_builder.h/.c

Fixed capacities, all failures are OutputTooLarge except where noted:

- MAX_TYPES 128, MAX_VALUES (constants) 512, MAX_METADATA 256,
  MAX_FUNCTIONS 32, MAX_NAMED_MD 32, MAX_SYMTAB_ENTRIES 64,
  MAX_FUNCTION_BODY_OPS 1024.
- Type helpers and dedup (`find_or_add_type`): void, i1, i8, i32, i64,
  f16, f32, pointer, function, array, vector, metadata, label are
  deduplicated by structural equality; a second `type_i32` returns the
  first index. Struct types are NEVER deduplicated (`type_eql` returns
  false for structs) and `type_struct` always appends.
- `type_function`: more than 8 params gives OutputTooLarge; equality
  compares return type, param list, param count, and vararg flag.
- `type_struct`: more than 16 members gives OutputTooLarge.
- `type_array` equality: element and length. `type_vector` equality:
  element and length. `type_pointer` equality: pointee index.
- There is NO `type_i16` helper in Zig (the i16 variant exists but is
  only reachable via a direct `add_type`); the C API mirrors this.
- `add_function`: assigns `value_index` from a monotonically increasing
  `next_value_id` starting at 0, in insertion order, and returns the
  function index. Capacity 32.
- `add_constant` / `add_const_i32` / `add_const_u32` /
  `add_const_f32`: constants consume value ids from the same counter,
  in insertion order. `add_const_i32` uses the i32 type;
  `add_const_u32` also uses the i32 type (not a u32 type: Zig has none);
  `add_const_f32` stores the f32 bit pattern zero-extended to u64.
- Metadata: string, value (type + value index), node (up to 16
  operands, each present index or null). Named metadata: up to 16 node
  indices. Symtab entries: value index plus name string (borrowed, not
  copied, exactly as Zig borrows the slice).
- `create_function_body(func_index)`: the body's `next_local_value`
  starts at the builder's current `next_value_id`. Out-of-range
  `func_index` is a Zig panic; the C port returns InvalidIr.
- `FunctionBody.add_op`: appends, stores the result type, returns the
  pre-increment `next_local_value`, then increments it. `add_void_op`:
  appends with result type INVALID_TYPE (`0xFFFFFFFF`) and does NOT
  advance `next_local_value`. Both fail with OutputTooLarge at 1024 ops.
- Zig test parity: distinct void/i32/f32 indices; i32 dedup; one
  function plus one i32 constant gives function_count 1 and
  constant_count 1.

## 4. dxil_serialize.zig -> dxil_serialize.h/.c

Exact emission order for `serialize(builder, bodies, out)`:

1. LLVM magic bytes.
2. MODULE block, abbrev width 3.
3. VERSION record {1}.
4. TRIPLE string record with `dxil-ms-dx`.
5. DATALAYOUT string record with the datalayout string from section 1.
6. TYPE block, abbrev width 4: NUMENTRY record {type_count}, then one
   record per type in insertion order:
   - void -> VOID {}; i1/i8/i16/i32/i64 -> INTEGER {width};
     f16 -> HALF {}; f32 -> FLOAT {}; f64 -> DOUBLE {};
     metadata -> METADATA {}; label -> LABEL {};
     pointer -> POINTER {pointee, 0};
     function -> FUNCTION {is_vararg, return_type, params...};
     struct -> if named, a STRUCT_NAMED string record with the name
       FIRST, then always a STRUCT_ANON record {0 (ispacked),
       members...} (the Zig code computes a STRUCT_NAMED code and then
       discards it; the record code is always STRUCT_ANON);
     array -> ARRAY {len, elem}; vector -> VECTOR {len, elem}.
7. Function declarations, in order, each a FUNCTION record with exactly
   15 operands: {type_index, 0 (calling conv), isproto (1 when NOT a
   definition), linkage, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}.
8. CONSTANTS block (only if constant_count > 0), abbrev width 4:
   a SETTYPE record {type_index} is emitted whenever the constant's
   type differs from the previous constant's type (starting from
   INVALID_TYPE, so the first constant always gets a SETTYPE); then
   per value: int -> INTEGER {signed-rotated encoding, same rotation
   as section 2}; uint -> INTEGER {value << 1}; float -> FLOAT {bits};
   null -> NULL {}; undef -> UNDEF {}.
9. METADATA block (only if md_count > 0 or named_md_count > 0), abbrev
   width 4: string -> STRING record; value -> VALUE {type, value};
   node -> NODE {operand+1 for a present index, 0 for null} (metadata
   indices are 1-based on the wire, 0 means null); then named metadata:
   NAME string record followed by NAMED_NODE {node indices AS-IS,
   no +1 adjustment} (asymmetry is Zig behavior, preserved).
10. One FUNCTION block per body, abbrev width 4: DECLAREBLOCKS
    {num_blocks}, then one record per op:
    - ret_void -> RET {}; ret_value -> RET {value};
    - call -> CALL {0 (attr), result_type, callee, args...};
    - binop -> BINOP {lhs, rhs, opcode};
    - cast -> CAST {operand, dest_type, opcode};
    - load -> LOAD {ptr, type, align_log2, 0};
    - store -> STORE {ptr, value, align_log2, 0};
    - alloca -> ALLOCA {type, type, count, align_log2 | (1 << 5)};
    - gep -> GEP {inbounds, base_type, ptr, indices...};
    - extract_value -> EXTRACTVAL {aggregate, index};
    - insert_value -> INSERTVAL {aggregate, value, index};
    - cmp -> CMP {lhs, rhs, predicate};
    - br -> BR {target}; br_cond -> BR {true_block, false_block, cond}
      (note the order: true, false, cond);
    - phi -> PHI {type, (value, block)...};
    - switch -> SWITCH {cond, default_block, (value, block)...}.
11. VALUE_SYMTAB block (only if symtab_count > 0), abbrev width 4:
    ENTRY records {value_index, name bytes}.
12. Exit MODULE, finalize. Any buffer exhaustion anywhere gives
    OutputTooLarge and the returned size is undefined (Zig propagates
    the error without a size).
- Zig test parity: a builder with void + i32 serializes to more than 4
  bytes starting with the LLVM magic.
- The serialize-time function lookup in Zig (`b.functions[call.callee]`,
  result discarded) has no observable output effect and is NOT copied;
  a callee value that would index out of range is therefore not an
  error in the C port (see gaps.md).

## 5. dxil_container.zig -> dxil_container.h/.c (+ dxil_md5.h/.c)

`write_dxil_program_part(header, out)`:

- total = 24 + bitcode padded up to a multiple of 4; total > out length
  gives OutputTooLarge; bitcode longer than `0xFFFFFFFF` gives
  OutputTooLarge (Zig would trap on the cast).
- Words, little-endian: program_version = (shader_kind << 16) |
  (shader_model_major << 4) | shader_model_minor (defaults 6 and 0);
  total program size in uint32 words; `DXIL` magic; dxil_version =
  (1 << 8) | 0 = 0x100; bitcode offset 16 relative to `DXIL` magic;
  unpadded bitcode size. This corrects the inherited Zig header layout
  against Microsoft's `DxilProgramHeader`, rather than preserving invalid bytes.
- Then the bitcode bytes, then zero padding to a 4-byte boundary.
- Example: compute kind 5, SM 6.0, 4-byte bitcode gives the words
  `0x00050060, 7, 0x4c495844, 0x00000100, 16, 4` followed by the bitcode, 28
  bytes total, no padding. A 5-byte bitcode pads with 3 zero bytes.

`write_container(parts, out)`:

- Header size = 32 + 4 * part_count. Body = sum over parts of
  (8 + data padded to a multiple of 4). Total = header + body; total >
  out length gives OutputTooLarge.
- Layout: `DXBC` magic; 16-byte hash field (zeroed, filled last);
  version 1 (u32 LE); total size (u32 LE); part count (u32 LE); offset
  table (one u32 LE per part, first offset = header size, each next
  advanced by 8 + padded data length); then per part: fourcc, u32 LE
  unpadded data length, data bytes, zero padding to 4.
- Hash: MD5 over the container bytes starting at offset 20 (i.e. after
  magic + hash field), written into bytes 4..20. If the container is 20
  bytes or shorter the hash is all zeros.
- CODE WINS OVER COMMENT: the Zig comment claims the first four hash
  bytes are XORed with the container size, but the code does no XOR;
  the C port implements the code (plain MD5 digest) and the test
  asserts the plain digest. The comment/code disagreement is recorded
  in gaps.md.
- `write_empty_signature`: exactly 8 bytes {0, 8} (element count 0,
  header byte size 8); too small a buffer gives OutputTooLarge.
- `write_feature_flags`: exactly 8 bytes {0, 0}; too small a buffer
  gives OutputTooLarge.
- Zig test parity: an empty container has size >= 32 and starts with
  `DXBC`; a one-part container (DXIL, bytes DE AD BE EF) has part
  count 1 at offset 28 and size > 32.
- MD5 unit vectors (RFC 1321, checked against Python hashlib):
  MD5("") = d41d8cd98f00b204e9800998ecf8427e;
  MD5("abc") = 900150983cd24fb0d6963f7d28e17f72;
  MD5("The quick brown fox jumps over the lazy dog") =
  9e107d9d372bb6826bd81d3542a419d6.
- Empty-container hash oracle: the empty container is 32 bytes; bytes
  20..32 are `01 00 00 00 20 00 00 00 00 00 00 00`; its MD5 (computed
  with Python hashlib during development and frozen into the test) is
  asserted byte-for-byte.

## 6. dxil_validate.zig -> dxil_validate.h/.c

`validate(data)` never fails by return code; it returns a result struct
(valid flag, container_size, part_count, has_dxil_part, has_hash_part,
shader kind, major, minor, bitcode size, error message). Checks, in
Zig order, with the first failure winning:

1. Length < 32 -> TooSmall ("container too small for DXBC header").
2. Bytes 0..4 != `DXBC` -> BadMagic ("invalid DXBC magic bytes").
3. u32 LE at 20 != 1 -> BadVersion ("unsupported DXBC version
   (expected 1)").
4. u32 LE at 24 != data length -> SizeMismatch ("container size field
   does not match data length").
5. u32 LE at 28 >= 256 -> PartCountTooLarge ("part count exceeds
   maximum (256)"). Note: 256 itself is rejected.
6. Offset table end 32 + 4 * part_count > length ->
   PartOffsetOutOfBounds ("part offset exceeds container bounds").
7. Per part: part offset + 8 > length -> PartOffsetOutOfBounds;
   part data start + part data size > length ->
   PartOffsetOutOfBounds.
8. For a DXIL part: part data size < 24 -> DxilPartTooSmall ("DXIL
   part too small for program header or bitcode"). From the program
   header: shader kind = (program_version >> 16) & 0xF, major =
   (program_version >> 4) & 0xF, minor = program_version & 0xF (these
   are the SHADER MODEL fields from word 0, even though the result
   struct names them dxil_major/dxil_minor). The word count at +4 must
   admit a complete header and fit the part. Magic at +8 must be `DXIL`.
   Bitcode offset is the word at +16, relative to that magic; bitcode
   size is the word at +20. Offsets before the bitcode header, sizes
   below four bytes, and spans beyond the declared program size reject
   as DxilPartTooSmall. Incorrect first bitcode bytes `42 43 C0 DE`
   reject as BadBitcodeMagic. These are Microsoft DXC's actual fields,
   correcting the inherited Zig layout rather than accepting two formats.
   A later DXIL part overwrites the kind/major/minor/bitcode fields
   of an earlier one.
9. After the loop, no DXIL part -> NoDxilPart ("no DXIL part found in
   container").
- On any failure the result reports has_dxil_part false,
  has_hash_part false, and all optional fields absent, even if a DXIL
  part was already seen; container_size is the u32 at 24 when length
  >= 28 else the length itself; part_count is the u32 at 28 when
  length >= 32 else 0. (This includes the TooSmall path for lengths
  28..31, where the size field is still readable.)
- A HASH part anywhere sets has_hash_part on success.
- Zig test parity: minimal valid container (compute kind, 4-byte
  magic bitcode) validates with container_size == size, part_count 1,
  has_dxil_part true, kind 5, bitcode_size >= 4, no error message;
  8-byte input fails mentioning "too small"; corrupted magic fails
  mentioning "magic"; version 2 fails mentioning "version"; inflated
  size field fails mentioning "size"; a two-part container with a
  20-byte HASH part validates with has_hash_part true, part_count 2,
  kind 1 (vertex).
- Additional C tests: part_count 256 rejected; container with only an
  ISGN part fails with NoDxilPart; DXIL part of 8 bytes fails with
  DxilPartTooSmall; DXIL part whose bitcode is `00 00 00 00` fails with
  BadBitcodeMagic; a part offset pointing past the end fails with
  PartOffsetOutOfBounds.

## 7. emit_dxil_native.zig -> dxil_mapping.h/.c (decision tables only)

The IR walk itself (statements, expressions, resource collection over
Doe IR) cannot be ported without a C Doe IR, and is listed in gaps.md
as unimplemented. What IS ported, as pure functions over scalar inputs,
is every decision table the walk uses:

- Stage mapping: compute -> ShaderKind COMPUTE (5), vertex -> VERTEX
  (1), fragment -> PIXEL (0). Stage -> shader-model metadata string:
  compute "cs", vertex "vs", fragment "ps".
- Entry-point function type: `void()` (no parameters); inputs arrive
  via dx.op intrinsics.
- Compute parameter builtins: global_invocation_id -> THREAD_ID (93),
  one dx.op call per component (3 calls, component constants 0, 1, 2);
  local_invocation_id -> THREAD_ID_IN_GROUP (95), 3 calls;
  workgroup_id -> GROUP_ID (94), 3 calls;
  local_invocation_index -> FLATTENED_THREAD_ID_IN_GROUP (96), exactly
  1 call with no component argument; any other builtin -> no call.
- Vertex parameter builtins: vertex_index or instance_index ->
  LOAD_INPUT (4), one call each; others -> no call.
- Fragment parameter builtins: a location, or position, or front_facing
  -> LOAD_INPUT (4), one call each; others -> no call.
- Builtin call mapping (name -> opcode, extra constant argument):
  workgroupBarrier -> BARRIER (80) with mode 1|8 = 9;
  storageBarrier -> BARRIER (80) with mode 1|2 = 3;
  textureBarrier -> BARRIER (80) with mode 1|2 = 3;
  subgroupElect -> WAVE_IS_FIRST_LANE (110), no argument;
  subgroupAll -> WAVE_ALL_TRUE (114), first argument passed through;
  subgroupAny -> WAVE_ANY_TRUE (113), first argument passed through;
  subgroupBallot -> WAVE_ACTIVE_BALLOT (116), first argument passed
  through; subgroupAdd -> WAVE_ACTIVE_OP (119) with first argument
  plus WaveOp SUM (0); any other builtin -> "other": arguments are
  still evaluated and the result is constant 0 (the C function reports
  OTHER, and the caller must preserve argument evaluation separately).
- Unary ops (float flag selects): neg -> FSUB if float else SUB, built
  as `0 - operand`; not / bit_not -> XOR, built as `operand ^ -1`
  (all-ones i32 constant).
- Binary ops (float flag, signed flag): add -> FADD/ADD, sub ->
  FSUB/SUB, mul -> FMUL/MUL, div -> FDIV, or SDIV if signed else UDIV,
  rem -> FREM, or SREM if signed else UREM, bit_and -> AND, bit_or ->
  OR, bit_xor -> XOR, shift_left -> SHL, shift_right -> ASHR if signed
  else LSHR, logical_and -> AND, logical_or -> OR. Comparisons return
  a CMP op, not a BINOP: equal -> FCMP_OEQ/ICMP_EQ, not_equal ->
  FCMP_ONE/ICMP_NE, less -> FCMP_OLT or ICMP_SLT/ICMP_ULT, less_equal
  -> FCMP_OLE or ICMP_SLE/ICMP_ULE, greater -> FCMP_OGT or
  ICMP_SGT/ICMP_UGT, greater_equal -> FCMP_OGE or ICMP_SGE/ICMP_UGE.
- Resource classification (address space, access, type kind -> class,
  kind, or none): uniform -> (CBV, CBUFFER); storage with read access
  -> (SRV, storage kind); storage with any other access (including a
  missing access, which Zig defaults to read_write) -> (UAV, storage
  kind); storage kind: array or struct -> STRUCTURED_BUFFER, anything
  else -> RAW_BUFFER; handle sampler / sampler_comparison ->
  (SAMPLER, SAMPLER); texture_1d -> (SRV, TEXTURE_1D); texture_2d,
  texture_2d_array, texture_depth_2d -> (SRV, TEXTURE_2D); texture_3d
  -> (SRV, TEXTURE_3D); texture_cube, texture_depth_cube ->
  (SRV, TEXTURE_CUBE); texture_multisampled_2d -> (SRV, TEXTURE_2D_MS);
  storage_texture_2d with read access -> (SRV, TEXTURE_2D), otherwise
  -> (UAV, TEXTURE_2D); missing address space, or any other handle
  type, or any other address space -> none (resource skipped).
- DXIL metadata values: dx.version node {1, 0}; dx.valver node
  {1, 7}; dx.shaderModel node {"cs"/"vs"/"ps", 6, 0}; compute
  dx.entryPoints node {entry name, null, null, properties node} where
  properties = {NUM_THREADS tag (4), numthreads node {x, y, z}};
  non-compute entryPoints node {entry name, null, null, null}.
- Container assembly order for a native module: SFI0, ISGN (empty
  signature), OSGN (empty signature), DXIL (program part), in that
  order. DXIL program part uses shader kind from the stage mapping.
- Module-size budgets (constants, not behavior): MAX_OUTPUT 256 KiB,
  internal bitcode budget 192 KiB, internal DXIL part budget 200 KiB,
  MAX_GLOBALS 64 (a 65th bound resource gives OutputTooLarge).
- NOT ported (see gaps.md): the statement/expression walk, the
  `param_ref` / `local_ref` / `global_ref` / `construct` / `member` /
  `index` placeholder constant-0 results, the `assign` no-store
  behavior, `address_of` / `deref` -> UnsupportedConstruct, and the
  full-module assembly from a Doe IR module.

## 8. emit_dxil.zig -> dxil_toolchain.h/.c (config, profile, DXC run, gate)

- Constants: MAX_OUTPUT 256 KiB, DXC_ENV_VAR `DOE_WGSL_DXC`,
  DXC_PATH_SENTINEL `PATH`, DXC output cap 64 KiB (not observable
  from the C API except as the runner's captured-output budget).
- Env-value parsing (`toolchainConfigFromEnvValue`): trim spaces,
  tabs, CR, LF from both ends; empty after trim ->
  ShaderToolchainUnavailable with a message naming `DOE_WGSL_DXC` and
  `PATH`; case-insensitive `PATH` -> discovery env_path_lookup,
  executable = platform default (`dxc` on Linux, `dxc.exe` on
  Windows); anything else -> discovery env_path, executable = the
  trimmed value.
- Unset variable -> discovery implicit_path_lookup, executable =
  platform default. (Zig distinguishes an allocation failure while
  reading the environment; the C port treats a getenv failure the same
  as unset, recorded in gaps.md.)
- Empty explicit config executable -> ShaderToolchainUnavailable with
  a message naming the env var and the PATH sentinel.
- Profile string: `{cs|vs|ps}_{major}_{minor}` from stage and shader
  model (compute "cs", vertex "vs", fragment "ps"; default model 6.0
  gives `cs_6_0`). A profile that does not fit the 16-byte Zig buffer
  is InvalidIr in Zig; the C port returns InvalidIr when the formatted
  string would not fit 16 bytes including the terminator.
- DXC argv, in order: executable, `-T`, profile, `-E`, entry name,
  `-Fo`, output path, input path.
- DXC run (POSIX on Linux): spawn failure (including executable not
  found) -> ShaderToolchainUnavailable with a discovery-specific
  message; nonzero exit -> InvalidIr with a message containing the
  discovery label, executable, exit code, profile, and entry name
  (plus captured output detail when non-empty); termination by a
  signal -> InvalidIr with an "unexpectedly terminated" message.
  Windows uses `CreateProcessW` with UTF-8 arguments, CRT quoting,
  restricted handle inheritance and concurrent bounded pipe capture.
  Launch failures use the same discovery diagnostics; all nonzero Windows
  exit codes (including NTSTATUS values) map to InvalidIr.
  `tools/test_dxil_windows.py` requires a real Windows SDK and DXC;
  cross-compilation alone is not Windows execution evidence.
- Output validation gate: after either emission path, the bytes are
  validated with section 6; an invalid container gives InvalidIr with
  the message `{producer} produced a structurally invalid DXIL
  container: {detail}` where producer is `native emitter` or `DXC`.
- Error mapping for the translate layer (section 9) is shared here.
- NOT ported (see gaps.md): the primary `emit` path (needs Doe IR and
  the native emitter), the HLSL-generation step of the DXC fallback
  (needs the HLSL emitter), temporary-file management under
  `.zig-cache/doe_wgsl_dxil`, and the thread-local compatibility
  diagnostic (the C API returns messages through caller buffers).

## 9. translate_dxil.zig -> dxil_toolchain.h (error mapping, constants)

This file is a thin adapter over WGSL analysis plus the emitter; both
sides are absent from the C port, so only the portable surface is
ported:

- MAX_OUTPUT, DXC_ENV_VAR, DXC_PATH_SENTINEL re-exported (same values
  as section 8).
- Error mapping, one to one: OutputTooLarge -> OutputTooLarge,
  UnsupportedBuiltin -> UnsupportedBuiltin, UnsupportedConstruct ->
  UnsupportedConstruct, InvalidIr -> InvalidIr, OutOfMemory ->
  OutOfMemory, ShaderToolchainUnavailable ->
  ShaderToolchainUnavailable. The mapping preserves a diagnostic
  detail string when present, else records the error without detail.
- NOT ported (see gaps.md): `translateToDxil*` themselves (they need
  the WGSL analyzer and the Doe IR), the arena-allocator lifetime
  difference between the two translate variants (one uses an arena
  that lives past the call, one frees the module), and the
  `compatibilityDiagnostic` global.

## 10. What the C tests must cover (all in tests/, built by Makefile)

- `test_dxil_bitcode.c`: section 2 behaviors, including exact VBR
  chunk bits, signed rotation values, block length patching verified
  by reading the length word back, depth-0 exit error, width-1 VBR
  error, and buffer exhaustion.
- `test_dxil_container.c`: section 5 behaviors, including the frozen
  empty-container MD5 oracle, program-part word layout, padding, and
  OutputTooLarge cases.
- `test_dxil_validate.c`: section 6 behaviors, including every Zig
  test plus the additional C-only cases.
- `test_dxil_builder_serialize.c`: sections 3 and 4, including dedup,
  struct uniqueness, value-id sequencing, capacity errors, record
  order (checked by decoding the bitstream with a small test-side
  reader: magic, MODULE enter, VERSION record), and serialize of a
  module with types, a function declaration, constants, metadata, a
  function body covering ret/binop/cmp/call/br, and a symtab entry.
- `test_dxil_mapping_toolchain.c`: sections 7, 8, 9, including every
  table row above, toolchain parsing (including case-insensitive
  `PATH`, whitespace trimming, empty rejection), profile strings,
  the validation gate message, and a DXC-run test against a fake
  `dxc` shell script (success writes the output file; exit 3 gives
  InvalidIr; a missing executable gives ShaderToolchainUnavailable).
- `test_dxil_md5.c`: section 5 MD5 vectors.
- Every test binary returns nonzero on any failure and prints one
  PASS/FAIL line per check group; `make check` runs all of them under
  both plain GCC and UBSan (plus ASan when it runs cleanly).

## 11. Doe IR subset (`ir.zig`, `ir_query.zig`, `layout_utils.zig`) -> dxil_ir.h/.c

Only the IR surface the DXIL walk reads, plus the query/layout helpers
the walk's neighbours use. Fixed capacities (Zig uses growable lists):
types 128, structs 32 (16 fields each), globals 128, functions 16,
params 32 per function, locals 64, expressions 512, expression args
512, statements 256, statement children 256, switch selectors 128,
switch cases 32, entry points 16. Exceeding a capacity fails with OutputTooLarge at the append
boundary; an out-of-range id fails with InvalidIr. All name strings are
borrowed, exactly as the builder borrows names; the C IR owns no heap
storage, so there is nothing to free, and the Zig arena-versus-allocator
distinction collapses (see section 15). A global initializer is kept
only as a scalar literal (bool, int, float): the DXIL walk never reads
initializers, and the HLSL emitter needs only scalar constants; a
composite initializer is outside the subset and the analyzer rejects
it with UnsupportedConstruct.

- Type interning mirrors `type_eql`: scalar, vector (elem, len),
  matrix (elem, columns, rows), array (elem, optional len), atomic,
  struct (by struct id), sampler, sampler_comparison, texture_1d/2d/
  2d_array/cube/multisampled_2d/3d (sample elem), texture_depth_2d,
  texture_depth_cube, storage_texture_2d (format, access), ref (elem,
  address space, access). A second intern of an equal type returns the
  first id; `INVALID_TYPE` (UINT32_MAX) never equals anything and
  `dxil_ir_type_get` on it fails with InvalidIr.
- `is_float_type`: scalar f32, f16, abstract_float true; vector recurses
  into its element; everything else false. `is_signed_type`: scalar
  i32, abstract_int true; vector recurses; everything else false.
  These are the native emitter's own helpers, not ir_query.
- Layout (storage, std430-compatible, from `layout_utils.zig`):
  alignment: f16 2, bool/i32/u32/f32/abstract_int/abstract_float 4,
  void and all other kinds 4; vector alignment is element alignment
  times lane multiplier (vec2 2, vec3/vec4 4, else 1); matrix alignment
  is element alignment times lane multiplier of rows; array alignment
  is element alignment; atomic alignment is inner alignment; struct
  alignment is the maximum field alignment (1 when empty). Size: f16 2,
  other scalars 4; vector is element size times len; matrix is column
  stride (column size rounded up to column alignment) times columns;
  fixed array is element stride (size rounded up to alignment) times
  len; runtime array size is 0; atomic size is inner size; struct size
  is the field walk rounded up to struct alignment. `round_up` with
  alignment <= 1 returns the value unchanged. `struct_field_offset`
  walks preceding fields with the same rounding. `minimum_binding_size`
  follows the Zig recursion (runtime array counts one element, struct
  end rounded up to struct alignment) and fails with InvalidIr when
  the result does not fit u32.
- Queries (from `ir_query.zig`, over the same fixed IR): compute-only
  module (no entry points is false, any non-compute entry is false);
  `referenceRootLocal` walks member/index/load chains to a local_ref,
  else null; `parameterIsAssigned` scans assign statements whose lhs
  chain roots at the parameter; `resolveValueAlias` follows load,
  single-argument construct, and const-local initializers;
  `resolveConstLocalInitializer` finds a const local_decl initializer;
  `resolveIndexableType` strips ref wrappers; `classifyBuiltinComponent`
  resolves a member of a builtin param to component 0/1/2 for x/y/z,
  else null; `matchIntLiteral`/`matchesIntLiteral` through aliases;
  `typeHasIoStructField`; `exprIsTexture1D`; `resolveRuntimeArrayElementStride`
  (runtime array only, stride is element size rounded up to alignment);
  `findGlobalBase` through index/member/load; `isLocalRef` through
  aliases; `stmtWritesLocal`, `stmtContainsExpr`, `exprContainsExpr`
  (recursive, out-of-range ids are false, never an error);
  `runtimeArrayWriteGuard` recognizes a robustness-generated `min`
  clamp on an assignment lhs (unclamped index plus arrayLength-based
  bound, where the bound is `arrayLength(...) - 1`); anything else is
  null. Query recursion is bounded by the fixed table sizes; a walk
  that would cycle (a hand-built IR whose load chain loops) is cut off
  after at most one pass over the expression table and returns null or
  false instead of looping forever (Zig would loop; this is a new
  divergence, recorded in gaps.md).

## 12. Native emitter (`emit_dxil_native.zig`) -> dxil_native.h/.c

`dxil_native_emit(module, out, out_len, written)` mirrors `emit` plus
`emit_module` in order, with the internal budgets as stack buffers
(bitcode 192 KiB, DXIL part 200 KiB) and the caller buffer as the only
container destination:

1. No entry points -> InvalidIr. Entry function index out of range
   -> InvalidIr (Zig panics). Entry stage other than compute/vertex/
   fragment cannot occur in the C enum.
2. Base types in order: void, i1, i32, f32 (deduped builder indices).
3. dx.op declared first: function type i32(i32, vararg), name
   `dx.op.i32`, external, not a definition. Entry function declared
   second: type void(), name from the IR function, external, a
   definition. Value-id order therefore starts dx.op 0, entry 1.
4. Resource collection: globals in order, skipped when unbound or
   unclassifiable (classification inputs translated from IR address
   space, access, and type kind through `dxil_classify_resource`;
   storage access missing defaults to read_write; the storage-texture
   access comes from the storage-texture type, not the global).
   A 65th bound resource fails with OutputTooLarge. Collected
   resources are NOT emitted anywhere in the output (Zig stores them
   and never reads them again); only the 65th-resource failure is
   observable.
5. Entry body, per stage, exactly as Zig: compute params with a
   builtin io emit dx.op calls (global_invocation_id THREAD_ID,
   local_invocation_id THREAD_ID_IN_GROUP, workgroup_id GROUP_ID, 3
   component calls with u32 constants 0, 1, 2; local_invocation_index
   FLATTENED_THREAD_ID_IN_GROUP, exactly 1 call, no component argument;
   other builtins emit nothing); vertex params with vertex_index or
   instance_index emit one LOAD_INPUT call each; fragment params with
   a location, position, or front_facing emit one LOAD_INPUT call each.
   Then the statement walk from the function root, then one trailing
   ret-void op.
6. Statement walk (recursive, depth capped at 64: deeper nesting fails
   with InvalidIr instead of recursing without bound): block iterates
   its statement children in order; local_decl evaluates its
   initializer when present and discards the value; expr evaluates and
   discards; assign evaluates ONLY the rhs and performs no store (the
   Zig placeholder, preserved: the lhs is never evaluated, so an
   address_of lhs does not fail); return_ evaluates its value when
   present; if_ evaluates the condition, then the then block, then the
   else block when present, in that order, unconditionally (both
   branches are emitted, Zig does not branch); loop_ evaluates init,
   condition, body, continuing in that order; switch_ evaluates the
   scrutinee, then every case body in order; break_/continue_/discard_
   emit nothing. The first error aborts the walk and is the reported
   error.
7. Expression walk (same depth cap): bool_lit -> i1 constant of the
   boolean as u64; int_lit -> i32-typed constant with the u64 literal
   (serialize encodes it as uint, value << 1); float_lit -> f32
   constant of the value cast to float; param_ref/local_ref/global_ref
   -> i32 constant 0 (the Zig placeholders, preserved); load ->
   recurse into the inner expression; address_of/deref ->
   UnsupportedConstruct; unary -> neg is `0 - operand` with the zero
   in the result type's floatness and the binop result type always
   i32, not/bit_not is `operand ^ -1` with result type i32; binary ->
   the mapping table (section 7) with comparison predicates as CMP
   ops with i1 result type and all other ops as BINOP with i32 result
   type; call with kind builtin -> the builtin table (barriers with
   mode constants, subgroup ops, other builtins evaluate every
   argument in order and return i32 constant 0); call with kind user
   -> i32 constant 0 WITHOUT evaluating arguments; construct -> i32
   constant 0 WITHOUT evaluating arguments; member -> evaluate the
   base, then i32 constant 0; index -> evaluate base, then index,
   then i32 constant 0. A dx.op call is one CALL op: result type i32,
   callee the dx.op function value index, first argument the opcode
   as a u32 constant, then the extra arguments in order.
8. Symbol table: one entry, the entry function value index with the
   IR function name.
9. DXIL metadata, in Zig order: `dx.version` node {1, 0};
   `dx.valver` node {1, 7}; `dx.shaderModel` node {"cs"/"vs"/"ps", 6,
   0}; `dx.entryPoints` node {entry name, null, null, properties}
   where compute properties are {NUM_THREADS tag (4), numthreads node
   {x, y, z}} and non-compute properties are null. Every metadata
   value is an i32-typed constant created through the builder in this
   order, so value-id sequencing matches Zig (metadata constants are
   created after body constants).
10. Serialize the builder with the single entry body into the bitcode
    buffer; build the DXIL program part (shader kind from the stage,
    shader model 6.0); build SFI0, empty ISGN, empty OSGN; assemble
    the container in exactly that order into the caller buffer.
    Buffer exhaustion anywhere is OutputTooLarge.
- Zig test parity: the Zig native test module (compute `main`,
  workgroup 64,1,1, one vec3<u32> param with global_invocation_id,
  empty block root) emits a container larger than 32 bytes starting
  with `DXBC`, validating with kind COMPUTE (5). The C test decodes
  the bitcode (test-side scanner over MODULE sub-blocks) and asserts:
  the entry function body contains exactly 3 CALL ops (the three
  THREAD_ID component loads) plus the trailing RET; a barrier builtin
  body contains exactly 1 CALL op; and the METADATA block string
  payloads include `dx.version`, `dx.valver`, `dx.shaderModel`,
  `dx.entryPoints`, and `cs`.
- Not ported: nothing in this file remains unported. The placeholders
  above are Zig behavior preserved deliberately, not gaps.

## 13. Diagnostic, primary emit, and DXC fallback (`emit_dxil.zig`, diagnostic.zig) -> dxil_diagnostic.h/.c, dxil_emit.h/.c

No thread-local diagnostic exists in C (task direction, and the C
style ban on hidden mutable state). Every function that reports a
message takes an explicit `DxilDiagnostic *` parameter; passing NULL
is the C analogue of Zig's compatibility-diagnostic variants (the
status is still returned, the message is dropped). The diagnostic
value mirrors Zig's allocation-free record: a 256-byte message buffer,
a 96-byte source-context buffer, stage, kind (absent when cleared),
line, column. `set` with a source and byte offset computes line,
column, and the context snippet exactly as Zig's `recordSourceContext`
(lines counted from 1, snippet windowed to 96 bytes around the token
when the line is longer). `set_detail` formats
`{stage}: {kind}: {detail}` into the message buffer. `clear` resets
stage to none, kind to absent, lengths to zero.

- `dxil_lower(module, lowered)`: no entry points -> InvalidIr; entry
  function index out of range -> InvalidIr (Zig indexes without a
  check); stage mapped compute/vertex/fragment; name is the IR
  function name (borrowed); workgroup size copied from the entry
  point; shader model defaults to 6.0 (Zig's `Module` default; the
  lowered struct carries no other fields).
- `dxil_emit_with_diagnostic(module, out, out_len, written,
  diagnostic)`: clears the diagnostic, runs the native emitter, then
  the validation gate with producer `native emitter`. On gate failure
  the status is InvalidIr and the diagnostic detail is
  `{producer} produced a structurally invalid DXIL container:
  {detail}` (detail from the validator, or `unknown validation
  failure` when absent), formatted through `set_detail` with stage
  dxil_emit, so the message reads
  `dxil_emit: InvalidIr: native emitter produced a structurally
  invalid DXIL container: ...`. A native-emitter failure itself
  returns its status with the diagnostic left cleared (Zig returns
  the error before any `setLastError` in that path, apart from errors
  the validator path sets).
- `dxil_emit(module, out, out_len, written, diagnostic_or_null)` is
  the same function with a nullable diagnostic: the C analogue of
  Zig's `emit` (which used the thread-local compatibility diagnostic).
- `dxil_emit_toolchain_with_diagnostic(module, out, out_len, config,
  cache_dir, written, diagnostic)`: clears the diagnostic; an empty
  executable fails with ShaderToolchainUnavailable and a message
  naming `DOE_WGSL_DXC` and `PATH` (Zig's empty-executable text);
  lowers the module (InvalidIr when there is no entry point);
  generates HLSL into a 256 KiB stack buffer (HLSL errors map
  OutputTooLarge/InvalidIr/UnsupportedBuiltin one to one, as Zig's
  catch maps them); creates `cache_dir` when needed (default
  `.zig-cache/doe_wgsl_dxil` when cache_dir is NULL, Zig's constant);
  writes `<cache>/<unique>.hlsl` and reserves `<cache>/<unique>.dxil`
  with a process-id plus counter suffix created with O_EXCL (Zig uses
  a nanosecond timestamp nonce; the C suffix differs, the uniqueness
  and cleanup properties do not); deletes both files before returning
  on every path, success or failure (Zig's deferred deletes); runs
  DXC through the existing `dxil_dxc_run` (spawn failure ->
  ShaderToolchainUnavailable with the runner's discovery message set
  as the diagnostic detail; nonzero exit or signal -> InvalidIr with
  the runner's message as the detail); a successful run that wrote no
  output file fails with InvalidIr and the message
  `DXC reported success via {label} \`{exe}\` but did not write
  \`{path}\``; an output file larger than 256 KiB fails with
  OutputTooLarge and the message `DXIL output \`{path}\` exceeded
  the 262144-byte contract`; output larger than the caller buffer
  fails with OutputTooLarge and no diagnostic detail (Zig returns the
  error without setting a message in that branch); otherwise the
  validation gate runs with producer `DXC` and the bytes are copied
  to the caller buffer. The 64 KiB DXC captured-output budget is the
  runner's, unchanged.
- Temp-file and HLSL details that differ from Zig are recorded in
  gaps.md (nonce scheme, no helper prologue in bounded HLSL).

## 14. Bounded HLSL emitter (`emit_hlsl.zig` and its maps/builtins, bounded) -> dxil_hlsl.h/.c

Sufficient for the DXC fallback path on compute modules in the IR
subset. Output goes to a caller buffer; exhaustion is OutputTooLarge;
the first error is the reported one. Types use Zig's spellings: void,
bool, int (i32, abstract_int), uint (u32), float (f32,
abstract_float), half (f16), vector as prefix plus len (uint3),
matrix as prefix plus columns `x` rows, struct by name, SamplerState,
Texture1D/2D/2DArray/Cube/MS/3D with `<float4>`, Texture2D/TextureCube
`<float>` for depth types, storage texture as Texture2D/RWTexture2D
`<float4>`. An array type used directly as a value type is InvalidIr
(Zig's `emit_type_only` rejects arrays); a typed name with a fixed
array appends `[len]`; a runtime array field in a struct is emitted
as `elem name[]`.

- Header: a single comment line naming the bounded emitter. This is a
  divergence: Zig emits a helper prologue (doe_atomic*, doe_pack*,
  doe_unpack*) before anything else. Because the prologue is absent,
  atomic and pack/unpack builtins fail here with UnsupportedBuiltin
  instead of emitting helper calls (recorded in gaps.md).
- Dispatch info: when any function parameter uses the num_workgroups
  builtin, the Zig cbuffer is emitted with the same name, register,
  fields, and pad, but laid out across lines by the C emitter
  (whitespace-only difference from Zig's single-line form):
  `cbuffer DoeDispatchInfo : register(b0, space7) {` then
  `uint3 doe_num_workgroups;` and
  `uint _doe_num_workgroups_pad;` indented, then `};`. Such a
  parameter is rendered in the body as
  `const uint3 <name> = doe_num_workgroups;`. Parameters with the
  subgroup_size or subgroup_invocation_id builtins are not entry
  parameters at all: a param_ref to them renders as the intrinsic
  call (`WaveGetLaneCount()` / `WaveGetLaneIndex()`).
- Structs: `struct <name> { <field type> <field name>; ... };`, one
  leading blank line per struct, 4-space indent.
- Globals, in module order: bound uniform -> 
  `cbuffer doe_uniform_<name> : register(b<binding>, space<group>) {
  <type> <name>; };`; bound storage -> `StructuredBuffer<elem>` for
  read access else `RWStructuredBuffer<elem>`, register `t` for read
  else `u`, with the array element type when the global type is an
  array; bound sampler/sampler_comparison -> `SamplerState <name> :
  register(s<binding>, space<group>);`; bound texture types ->
  `<Texture type> <name> : register(t<binding>, space<group>);`;
  bound storage texture -> `Texture2D<float4>` for read else
  `RWTexture2D<float4>`, register `t`/`u`; a bound global with any
  other address space or type is InvalidIr (Zig's fallthrough). An
  unbound workgroup global -> `groupshared <type> <name>;`; unbound
  private -> `static <type> <name>;`; unbound const/override ->
  `static const <type> <name>;`, each with a scalar initializer when
  present (` = <literal>`). Other unbound globals emit nothing.
- Functions: every module function, in order. A compute function
  renders `[numthreads(x, y, z)]` on its own line, then
  `<return type> <name>(<params>) { <body> }`. A function whose stage
  is vertex or fragment fails with UnsupportedConstruct: the Zig
  stage wrapper (impl function plus output struct plus wrapper) is
  not part of this bounded emitter (recorded in gaps.md); the native
  path still covers those stages. A non-entry helper function renders
  without numthreads. Parameters: ref types render `in` for read
  access else `inout` plus the element typed name; other params render
  the typed name; a param with a builtin io renders ` : <semantic>`
  from the builtin map (position SV_Position, frag_depth SV_Depth,
  front_facing SV_IsFrontFace, global_invocation_id
  SV_DispatchThreadID, local_invocation_id SV_GroupThreadID,
  local_invocation_index SV_GroupIndex, workgroup_id SV_GroupID,
  sample_index SV_SampleIndex, sample_mask SV_Coverage, vertex_index
  SV_VertexID, instance_index SV_InstanceID, clip_distances
  SV_ClipDistance, primitive_index SV_PrimitiveID); a builtin with no
  semantic (num_workgroups handled above, subgroup_size,
  subgroup_invocation_id, none with a location) follows Zig: the
  intrinsic builtins are skipped as parameters, num_workgroups is
  skipped as a parameter, a location-only param without a builtin is
  InvalidIr in Zig's `emit_param` and is InvalidIr here too. Local
  names `signed`/`unsigned` are rewritten to `__doe_local_<index>`
  (Zig's local-name rule).
- Statements mirror Zig's HLSL statement forms, with the loop
  break-flag machinery for continuing blocks: block renders braces
  with indented contents; local_decl renders `[const ]<type> <name>
  [= <expr>];`, and a ref-typed local is InvalidIr (Zig's pointer
  capture machinery is not in the bounded emitter); expr renders the
  expression plus `;`; assign renders `<lhs> <op> <rhs>;` with the
  operator spellings from `operator_text.zig` (`=`, `+=`, `-=`,
  `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`), without the
  runtime-array write guard (the analyzer never generates the guard
  pattern, so the guard never triggers on analyzer-produced IR;
  hand-built IR containing it is a recorded divergence); return
  renders `return [<expr>];`; if renders `if (<cond>) { ... }` with
  an `else` block when present; loop renders the Zig while form:
  when init or continuing is present the whole loop is wrapped in an
  extra block, init is emitted first, a
  `bool __doe_loop_break_<stmt id> = false;` guard is emitted when a
  continuing block exists, the condition is
  `!__doe_loop_break_<id> && (<cond>)` with a continuing block else
  the condition or `true`, the body follows, then the continuing
  block; switch renders `switch (<expr>) {` then, per case, one
  `case <selector>:` line per selector (or one `default:` line), then
  the case body block, then `break;`, then `}`; break renders
  `break;`, continue renders `continue;` except inside a loop with a
  continuing block, where it fails with UnsupportedConstruct (the
  Zig break-flag dance for that case is not in the bounded emitter);
  discard renders `discard;`.
- Expressions mirror Zig's HLSL expression forms: bool as true/false;
  int as decimal; float as shortest-roundtrip decimal with `.0`
  appended when the text has no `.` and no exponent (Zig's
  `write_float` rule; the C shortest search tries precisions 1..17
  and keeps the first that round-trips, which agrees with Zig's
  shortest output for ordinary literals and may differ in the last
  digit for exotic doubles, recorded in gaps.md); param_ref renders
  the intrinsic call for subgroup builtins else the param name;
  local_ref renders the local name, or, for a ref-typed local, the
  const initializer expression (InvalidIr when there is none, as
  Zig); global_ref renders the global name; load/address_of/deref
  render the inner expression; unary renders `(<op><operand>)`;
  binary renders `(<lhs> <op> <rhs>)` with the operator spellings
  (`+ - * / % & | ^ << >> == != < <= > >= && ||`); call with kind
  user renders `<name>(<args>)`; call with kind builtin renders the
  builtin table: workgroupBarrier ->
  `GroupMemoryBarrierWithGroupSync()`, storageBarrier ->
  `AllMemoryBarrierWithGroupSync()`, textureBarrier ->
  `DeviceMemoryBarrierWithGroupSync()`, bitcast ->
  `asfloat`/`asint`/`asuint` by result scalar (f32/f16/abstract_float
  -> asfloat, i32/abstract_int -> asint, else asuint) applied to the
  single argument (any other argument count is InvalidIr), select ->
  `((<cond>) ? (<true>) : (<false>))` with Zig's argument order
  (arg2 condition, arg1 true, arg0 false; 3 arguments required),
  subgroupShuffleXor -> `WaveReadLaneAt(<v>, WaveGetLaneIndex() ^
  <mask>)`, subgroupBallot -> `WaveActiveBallot(<v>)`,
  subgroupElect -> `WaveIsFirstLane()`, countLeadingZeros ->
  `((<x> == 0u) ? 32u : (31u - firstbithigh(<x>)))`,
  countTrailingZeros ->
  `((<x> == 0u) ? 32u : firstbitlow(<x>))`, renamed builtins
  (fract->frac, inverseSqrt->rsqrt, mix->lerp,
  countOneBits->countbits, reverseBits->reversebits,
  firstLeadingBit->firstbithigh, firstTrailingBit->firstbitlow,
  subgroupAdd->WaveActiveSum, subgroupExclusiveAdd->WavePrefixSum,
  subgroupMin->WaveActiveMin, subgroupMax->WaveActiveMax,
  subgroupBroadcast/subgroupShuffle->WaveReadLaneAt,
  subgroupAnd->WaveActiveBitAnd, subgroupOr->WaveActiveBitOr,
  subgroupXor->WaveActiveBitXor, subgroupAll->WaveActiveAllTrue,
  subgroupAny->WaveActiveAnyTrue), passthrough builtins emitted under
  their own name (abs, acos, asin, atan, atan2, ceil, clamp, cos,
  cosh, cross, determinant, distance, dot, exp, exp2, floor, fma,
  ldexp, length, log, log2, max, min, normalize, pow, reflect,
  refract, round, saturate, sign, sin, sinh, smoothstep, sqrt, step,
  tan, tanh, transpose, trunc); any other builtin name fails with
  UnsupportedBuiltin. Construct renders `<type>(<args>)`, except a
  single-scalar-argument vector construct renders
  `((<type>)(<arg>))` (Zig's splat form). Member renders
  `<base>.<field>`; index renders `<base>[<index>]`.
- Tests assert exact HLSL text for the canonical compute module (with
  the bounded header comment, not Zig's prologue) and for a module
  with a storage buffer global, a barrier call, and arithmetic; and
  UnsupportedBuiltin for an atomic builtin, UnsupportedConstruct for
  a vertex entry function.

## 15. WGSL subset analyzer and translate entry points (`pipeline/analysis.zig`, `pipeline/translate_dxil.zig`, bounded) -> dxil_analyze.h/.c, dxil_translate.h/.c

The full WGSL frontend (tokenizer, parser, sema, ir_builder,
ir_validate, robustness transform, opt rewrite) is thousands of lines
outside the DXIL directory and is NOT ported. The analyzer here is a
bounded subset that genuinely parses WGSL text into the section 11 IR
for the modules the DXIL paths consume, and fails visibly for
everything outside it. It is not a stub that accepts text and ignores
it: every accepted construct builds the IR the emitters walk, and
every rejected construct returns a translate status with a diagnostic
message naming the stage and the failure.

- Translate status set (mirrors `analysis.TranslateError`): InvalidWgsl,
  InvalidIr, DuplicateSymbol, InvalidAttribute, InvalidType,
  OutputTooLarge, OutOfMemory, ShaderToolchainUnavailable,
  UnexpectedToken, TypeMismatch, UnknownIdentifier, UnknownType,
  UnsupportedBuiltin, UnsupportedConstruct, UnsupportedPattern,
  UnsupportedWgsl. The emit-to-translate error map is exhaustive over
  the emit status set: OutputTooLarge, UnsupportedBuiltin,
  UnsupportedConstruct, InvalidIr, OutOfMemory,
  ShaderToolchainUnavailable map to the same-named translate status.
- Lexer: identifiers, keywords, integer literals (decimal and hex,
  with `u`/`i` suffixes stripped for the value), float literals (with
  `f` suffix stripped), punctuation, `//` line comments and
  `/* */` block comments (nesting is not supported; an unterminated
  comment is UnexpectedToken), string-free (WGSL has no string
  literals in this subset). Any other character is UnexpectedToken.
- Declarations, in order: `struct <Name> { <field>: <type>, ... };`
  (trailing comma allowed); `var[<addr>[, <access>]] <name> :
  <type>;` and `var <name> : <type>;` for unbound globals, each
  optionally preceded by `@group(<n>) @binding(<n>)` in either order;
  `const <name> : <type> = <scalar literal>;` (composite initializers
  are UnsupportedConstruct); `fn <name>(<params>) [-> <type>] {
  <statements> }` optionally preceded by `@compute`, `@vertex`, or
  `@fragment` and by `@workgroup_size(<x>[, <y>[, <z>]])` (a stage
  attribute on a function makes it an entry point; a function without
  a stage attribute is a helper, not an entry point; a workgroup_size
  on a non-compute entry is InvalidAttribute in Zig's sema and is
  InvalidAttribute here). A duplicate struct, global, or function
  name is DuplicateSymbol. An `@` attribute that is not recognized
  where attributes are allowed is InvalidAttribute. Anything else at
  top level is UnexpectedToken.
- Types: `void`, `bool`, `i32`, `u32`, `f32`, `f16`, `abstract_int`,
  `abstract_float` (accepted as spellings; sema would normally reject
  abstract types in declarations, the subset analyzer accepts them
  because the IR carries them), `vec2|3|4<<scalar>>`,
  `mat<C>x<R><<scalar>>`, `array<<elem>>`, `array<<elem>, <n>>`,
  `atomic<<scalar>>`, `sampler`, `sampler_comparison`,
  `texture_1d|2d|2d_array|3d|cube|multisampled_2d<<sample>>`,
  `texture_depth_2d`, `texture_depth_cube`,
  `texture_storage_2d<<format>, <access>>` with formats
  rgba8unorm, rgba8snorm, rgba8uint, rgba8sint, rgba16uint, rgba16sint,
  rgba16float, r32uint, r32sint, r32float, rg32uint, rg32sint,
  rg32float, rgba32uint, rgba32sint, rgba32float, and a previously
  declared struct name. Any other type name is UnknownType. A vector
  element that is not scalar is InvalidType; a matrix element that is
  not f32/f16/abstract_float is InvalidType.
- Parameters: zero or more `@builtin(<name>)` / `@location(<n>)`
  attributes (either order, at most one of each; a duplicate is
  InvalidAttribute), then `<name>: <type>`, comma separated. A
  builtin name that is not in the IR builtin set is
  UnsupportedBuiltin at parse time (Zig sema reports unknown builtins
  the same way for entry parameters). A parameter named the same as
  another parameter of the same function is DuplicateSymbol.
- Statements: block `{ ... }`; `let <name> [= <expr>];` and
  `let <name>: <type> [= <expr>];`; `var <name> [= <expr>];` and
  `var <name>: <type> [= <expr>];` (a local without an initializer
  and without a type is InvalidWgsl; `const` locals are `let` with
  is_const true); expression statement `<expr>;`; assignment
  `<expr> <assign-op> <expr>;` for `= += -= *= /= %= &= |= ^= <<=
  >>=`; `return [<expr>];`; `if (<expr>) <block> [else <block> | else
  if ...]` (else-if chains are flattened into a nested if_ in the
  else slot, as the IR represents them); `loop { ... }` and
  `while (<expr>) <block>` (both become loop_ nodes: loop has no
  condition, while carries one; a `for` loop is UnsupportedConstruct:
  the IR carries it, but the subset parser does not build its init
  statement from header syntax); `switch <expr> { case <expr>[,
  <expr>]*: <block> ... default: <block> }` (a case body that is not
  a block is UnexpectedToken; a switch with no cases is InvalidWgsl);
  `break;`, `continue;`, `discard;`. A missing `;` or `}` is
  UnexpectedToken. A `return` with a value in a void function, or a
  value-less return in a non-void function, is TypeMismatch (the
  subset analyzer checks only this return-shape rule, not full type
  checking: full sema type checking is out of scope and is recorded
  in gaps.md).
- Expressions, with C-like precedence (lowest to highest): `||`,
  `&&`, `|` (bitwise or), `^`, `&`, `==`/`!=`, relational
  `<`/`<=`/`>`/`>=`, shift `<<`/`>>`, additive `+-`, multiplicative
  `*/%`, unary (`-`, `!`, `~`), postfix (call, member, index),
  primary. Primary forms: integer, float, `true`, `false`, an
  identifier (resolved in order: function parameter, then local,
  then global; an unresolvable identifier is UnknownIdentifier),
  a parenthesized expression, a type-name construction
  `<type>(<args>)` (a struct name, scalar, vector, matrix, or array
  type followed by `(` starts a construct; the callee type is
  interned and the argument ids are stored in the function argument
  table), and a call `<name>(<args>)` (kind is fixed up after the
  whole module is parsed: a callee name that matches a module
  function is kind user, otherwise kind builtin; this mirrors sema's
  resolution for the subset). Member access stores the field name and
  its index in the struct definition when the base type is a known
  struct (index UINT32_MAX and field name as written when the base
  type is a vector, whose components are x/y/z/w, or when the base
  type is unknown to the subset). Indexing stores base and index.
  An identifier followed by `(` where the identifier is neither a
  type name nor a known function is still a builtin call (unknown
  builtins are diagnosed by the emitters, not the analyzer, except
  for entry-parameter builtins above).
- Locals: a local declared by `let`/`var` is registered in the
  function local table at its declaration point, so later statements
  resolve it and earlier ones do not (use before declaration is
  UnknownIdentifier). A local name that duplicates a parameter or an
  earlier local of the same function is DuplicateSymbol. Function
  bodies are parsed with the function's own parameter and local
  tables; a helper function body may reference globals but not
  another function's locals.
- Entry points: after parsing, every staged function becomes an entry
  point in declaration order, with the stage from its attribute and
  the workgroup size from its workgroup_size attribute (default
  1,1,1; a workgroup_size component larger than UINT32_MAX is
  InvalidWgsl). The module's first entry point is the one the native
  emitter uses (Zig uses entry_points[0] as well).
- What the subset analyzer does NOT do (recorded in gaps.md):
  full sema type checking (operand type rules, address-space rules,
  binding validation beyond group/binding presence, attribute
  argument validation beyond the rules above); pipeline override
  constants (translate entry points in Zig pass no overrides);
  ir_validate; the robustness transform (bounds-elision preconditions
  are never generated, so no dispatch precondition is ever recorded);
  the opt rewrite. A module that Zig's full pipeline would reject
  only through one of those passes may be accepted here; a module
  that Zig's pipeline accepts and that lies inside the subset grammar
  parses to the same IR shape the DXIL walk consumes.
- Translate entry points, all taking WGSL source text and a caller
  output buffer, all returning true on success (byte count written)
  and false on failure (translate status in *out_error when
  non-NULL, message in the diagnostic when non-NULL):
  - `dxil_translate_with_diagnostic(source, out, out_len, written,
    diagnostic)` (native path): analyze into function-local IR
    storage, then emit with a separate internal diagnostic; on emit
    failure the caller diagnostic receives stage dxil_emit, the
    mapped kind, and, when the internal diagnostic message is
    non-empty, that whole message as its detail (Zig's double-prefix
    shape is preserved deliberately: the caller message reads
    `dxil_emit: <Kind>: dxil_emit: <Kind>: <detail>` for gate
    failures; when the internal message is empty the caller diagnostic
    records stage dxil_emit and the kind with no detail). On analyze
    failure the analyzer's diagnostic stands and its status is
    returned unchanged.
  - `dxil_translate_toolchain_with_diagnostic(source, out, out_len,
    config, cache_dir, written, diagnostic)` (DXC fallback path):
    analyze into function-local IR storage, then the toolchain emit
    from section 13 with the same error mapping and diagnostic rule.
  - `dxil_translate(source, out, out_len, written,
    diagnostic_or_null)` and `dxil_translate_toolchain(source, out,
    out_len, config, cache_dir, written, diagnostic_or_null)` are the
    nullable-diagnostic analogues of Zig's compatibility-diagnostic
    variants (no thread-local storage in C).
  - Lifetime model, explicit: Zig's first variant allocates the IR
    module in an arena freed at return, and its toolchain variant
    allocates with the caller allocator and deinits after emit. In C
    the IR storage is function-local in both variants, the output
    bytes are caller-owned and never alias IR storage, and nothing is
    heap allocated, so there is no leak in either variant and no
    observable difference between them; the two variants exist so
    call sites name the path they chose, exactly as in Zig.
  - Toolchain config for the translate entry points is the existing
    toolchain config shape: an executable string (borrowed) plus a
    discovery value; the environment-parsing helpers in
    dxil_toolchain.h build it from `DOE_WGSL_DXC`.
- Tests: a canonical compute WGSL source
  (`@compute @workgroup_size(64, 1, 1) fn main(@builtin(global_invocation_id)
  gid: vec3<u32>) {}`) translates through the native path to a
  container that validates with kind COMPUTE and a bitcode body with
  exactly 3 CALL ops; a WGSL source with a storage buffer global, a
  workgroup barrier call, and arithmetic translates and validates; a
  vertex source (`@vertex fn main(@builtin(vertex_index) vi: u32) ->
  ...` in the subset grammar) translates through the native path to
  kind VERTEX; an unknown identifier fails with UnknownIdentifier and
  a diagnostic naming the sema stage; a syntax error fails with
  UnexpectedToken; an unknown type fails with UnknownType; a
  duplicate function name fails with DuplicateSymbol; the toolchain
  translate variant against a fake DXC script (which copies a
  pre-made valid container to the `-Fo` path) succeeds and returns
  the container bytes; an empty executable fails with
  ShaderToolchainUnavailable; a missing executable fails with
  ShaderToolchainUnavailable.
