#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Keep frozen source unchanged; lift documented pointer and subgroup-input negatives."""
from pathlib import Path
import sys

source = Path(__file__).with_name("test_hlsl.c").read_text()
old = '''        char output[16] = "intact";
        DoeHlslResult result;
        assert(doe_hlsl_emit(&f->source, f->module, 0u, f->scratch,
            output, sizeof(output), &result) == DOE_COMPILER_UNSUPPORTED);
        assert(result.written_bytes == 0u && result.required_bytes == 0u);
        assert(strcmp(output, "intact") == 0);'''
new = '''        char *output = emit(f, 0u);
        /* Both unchanged programs now lower to actual lvalue/inout operations. */
        /* Scalar constants are written at their use. */
        assert(strstr(output, " = 0x00000007u;") != NULL);
        if (i == 0u) assert(strstr(output, "inout uint") != NULL);
        free(output);'''
assert source.count(old) == 1, "Frozen pointer control drifted; review the adapter"
source = source.replace(old, new)
old = '''    f->module->entry_inputs[0].kind = INPUT_NUM_SUBGROUPS;
    DoeHlslResult result;
    char output[8] = "intact";
    assert(doe_hlsl_emit(&f->source, f->module, 0u, f->scratch, output, sizeof(output), &result) == DOE_COMPILER_UNSUPPORTED);
    assert(strcmp(output, "intact") == 0);
    f->module->entry_inputs[0].kind = INPUT_SUBGROUP_ID;
    assert(doe_hlsl_emit(&f->source, f->module, 0u, f->scratch, output, sizeof(output), &result) == DOE_COMPILER_UNSUPPORTED);'''
new = '''    release(f);
    f = prepare("enable subgroups; @group(0)@binding(0)var<storage,read_write>o:array<u32>;"
        "@compute @workgroup_size(32)fn main(@builtin(num_subgroups)n:u32,@builtin(subgroup_id)i:u32){o[i]=n;}");
    text = emit(f,0u);
    has(text,"groupshared uint doe_wave_count;");
    has(text,"InterlockedAdd(doe_wave_count");
    has(text,"WaveReadLaneFirst");
    free(text);'''
assert source.count(old) == 1, "Frozen subgroup-input control drifted; review adapter"
Path(sys.argv[1]).write_text(source.replace(old,new))
