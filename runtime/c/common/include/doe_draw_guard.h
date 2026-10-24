/* SPDX-License-Identifier: MIT */
#ifndef DOE_DRAW_GUARD_H
#define DOE_DRAW_GUARD_H
#include <stddef.h>
#include <string.h>

enum { DOE_DRAW_ARGUMENT_SOURCE_CAPACITY = 1024 };
typedef enum doe_draw_guard_source_status {
    DOE_DRAW_GUARD_SOURCE_OK, DOE_DRAW_GUARD_SOURCE_INVALID_ARGUMENT,
    DOE_DRAW_GUARD_SOURCE_TOO_SMALL
} doe_draw_guard_source_status;

/* Words 0..3 (nonindexed) or 0..4 (indexed) are copied GPU arguments. The host
 * writes capacities and the mode word before the copy. Comparisons never form
 * first+count, so wrapping sums suppress the draw instead of passing. */
#define DOE_DRAW_PREFIX \
"@group(0) @binding(0) var<storage, read_write> record: array<u32>;\n" \
    "@compute @workgroup_size(1) fn main() {\n" \
    "    let count = record[0];\n" \
    "    let instance_count = record[1];\n" \
    "    let first = record[2];\n" \
    "    let indexed = (record[7] & 1u) != 0u;\n" \
    "    let first_allowed = (record[7] & 2u) != 0u;\n" \
    "    var first_instance = record[3];\n" \
    "    var count_limit = record[4];\n" \
    "    var instance_limit = record[5];\n" \
    "    if (indexed) {\n" \
    "        first_instance = record[4];\n" \
    "        count_limit = record[5];\n" \
    "        instance_limit = record[6];\n" \
    "    }\n" \
    "    if (count == 0u || instance_count == 0u || (first_instance != 0u && !first_allowed) ||\n"
#define DOE_DRAW_SUFFIX \
"        record[0] = 0u;\n        record[1] = 0u;\n" \
    "        record[2] = 0u;\n        record[3] = 0u;\n" \
    "        if (indexed) {\n            record[4] = 0u;\n        }\n" \
    "    }\n}\n"
static const char doe_draw_argument_wgsl[] = DOE_DRAW_PREFIX
    "        first > count_limit || count > count_limit - first ||\n"
    "        first_instance > instance_limit || instance_count > instance_limit - first_instance) {\n"
    DOE_DRAW_SUFFIX;
/* V2 carries exact 64-bit capacities as low/high words. first/count are u32;
 * their mathematical sum has at most one carry, never a wrapped bound. */
static const char doe_draw_argument_v2_wgsl[] =
    "fn fits(first:u32,count:u32,low:u32,high:u32)->bool {\n"
    "    let end=first+count;\n"
    "    let carry=select(0u,1u,end<first);\n"
    "    return carry<high || (carry==high && end<=low);\n}\n"
    DOE_DRAW_PREFIX
    "        !fits(first,count,count_limit,record[8]) ||\n"
    "        !fits(first_instance,instance_count,instance_limit,record[9])) {\n"
    DOE_DRAW_SUFFIX;
#undef DOE_DRAW_PREFIX
#undef DOE_DRAW_SUFFIX


/* Exact retained normalizer text: backends own native capacities, scratch,
 * ordering and completion leases; the compiler alone owns WGSL meaning.
 * Failure preserves caller source/length storage. */
static inline doe_draw_guard_source_status doe_draw_argument_source(
    char *out, size_t capacity, size_t *out_length) {
    if (!out || !out_length) return DOE_DRAW_GUARD_SOURCE_INVALID_ARGUMENT;
    if (capacity < sizeof(doe_draw_argument_wgsl)) return DOE_DRAW_GUARD_SOURCE_TOO_SMALL;
    memcpy(out, doe_draw_argument_wgsl, sizeof(doe_draw_argument_wgsl));
    *out_length = sizeof(doe_draw_argument_wgsl) - 1;
    return DOE_DRAW_GUARD_SOURCE_OK;
}
/* V2 record: original words0..7 plus count-capacity high word8 and instance-
 * capacity high word9. Low-word positions and mode bits retain legacy meaning.
 * Shader source/version and this 40-byte record contract are inseparable. */
enum { DOE_DRAW_GUARD_RECORD_V2_BYTES = 40, DOE_DRAW_GUARD_SOURCE_V2_CAPACITY = 1536 };
static inline doe_draw_guard_source_status doe_draw_argument_source_v2(
    char *out, size_t capacity, size_t *out_length) {
    if (!out || !out_length) return DOE_DRAW_GUARD_SOURCE_INVALID_ARGUMENT;
    if (capacity < sizeof(doe_draw_argument_v2_wgsl)) return DOE_DRAW_GUARD_SOURCE_TOO_SMALL;
    memcpy(out, doe_draw_argument_v2_wgsl, sizeof(doe_draw_argument_v2_wgsl));
    *out_length = sizeof(doe_draw_argument_v2_wgsl) - 1;
    return DOE_DRAW_GUARD_SOURCE_OK;
}
/* Scan runs only after the same V2 argument normalizer. Its record loads /
 * cancellation stores are atomic: a rejected index cancels the draw without
 * racing the other scanners. User indices and first/base fields never change.
 * Params words: window ordinal low/high, element count, initial halfword,
 * uint16 flag, vertex capacity low/high, strip-restart flag, invocation stride.
 * The native owner supplies checked bounded windows and a nonzero stride. */
enum { DOE_DRAW_INDEX_SCAN_SOURCE_CAPACITY = 3072, DOE_DRAW_INDEX_SCAN_PARAMS_BYTES = 36 };
static const char doe_draw_index_scan_wgsl[] =
    "@group(0) @binding(0) var<storage,read_write> record:array<atomic<u32>>;\n"
    "@group(0) @binding(1) var<storage,read> indices:array<u32>;\n"
    "@group(0) @binding(2) var<storage,read> p:array<u32>;\n"
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gid:vec3u) {\n"
    "    let first=atomicLoad(&record[2]);\n"
    "    let count=atomicLoad(&record[0]);\n"
    "    let end=first+count;\n"
    "    let high=select(0u,1u,end<first);\n"
    "    if(count==0u || high<p[1] || (high==p[1] && end<=p[0])) { return; }\n"
    "    let borrow=select(0u,1u,end<p[0]);\n"
    "    let available=end-p[0];\n"
    "    let last=select(min(available,p[2]),p[2],high-p[1]>borrow);\n"
    "    var begin=0u;\n"
    "    if(p[1]==0u && first>p[0]) { begin=min(first-p[0],p[2]); }\n"
    "    if(begin>=last || gid.x>=last-begin) { return; }\n"
    "    var local=begin+gid.x;\n"
    "    loop {\n"
    "        if(local>=last || atomicLoad(&record[0])==0u) { break; }\n"
    "        {\n"
    "            var index=0u;\n"
    "            if(p[4]!=0u) {\n"
    "                let half=p[3]+local;\n"
    "                index=(indices[half/2u]>>((half&1u)*16u))&65535u;\n"
    "            } else {\n"
    "                index=indices[local];\n"
    "            }\n"
    "            let restart=select(0xffffffffu,65535u,p[4]!=0u);\n"
    "            if(p[7]==0u || index!=restart) {\n"
    "                let base=atomicLoad(&record[3]);\n"
    "                var value=index+base;\n"
    "                var carry=select(0u,1u,value<index);\n"
    "                var valid=true;\n"
    "                if(bitcast<i32>(base)<0i) {\n"
    "                    let magnitude=0u-base;\n"
    "                    valid=index>=magnitude;\n"
    "                    value=index-magnitude;\n"
    "                    carry=0u;\n"
    "                }\n"
    "                valid=valid && (carry<p[6] || (carry==p[6] && value<p[5]));\n"
    "                if(!valid) {\n"
    "                    atomicStore(&record[0],0u);\n"
    "                    atomicStore(&record[1],0u);\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "        if(p[8]>=last-local) { break; }\n"
    "        local=local+p[8];\n"
    "    }\n"
    "}\n";
static inline doe_draw_guard_source_status doe_draw_index_scan_source(
    char *out, size_t capacity, size_t *out_length) {
    if (!out || !out_length) return DOE_DRAW_GUARD_SOURCE_INVALID_ARGUMENT;
    if (capacity < sizeof(doe_draw_index_scan_wgsl)) return DOE_DRAW_GUARD_SOURCE_TOO_SMALL;
    memcpy(out, doe_draw_index_scan_wgsl, sizeof(doe_draw_index_scan_wgsl));
    *out_length = sizeof(doe_draw_index_scan_wgsl) - 1;
    return DOE_DRAW_GUARD_SOURCE_OK;
}
#endif
