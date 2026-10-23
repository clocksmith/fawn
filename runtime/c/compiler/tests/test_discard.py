#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Nonterminating fragment discard and shared ceiling owner developer checks."""
import struct
import unittest
import test_stages


class Discard(unittest.TestCase):
    compile = test_stages.Stages.compile

    def test_helper_demotion_preserves_following_code_and_return(self):
        code = self.compile('''
            @group(0) @binding(0) var<storage,read_write> value:u32;
            fn helper()->u32 {value=1u;discard;value=2u;return 3u;}
            @fragment fn main()->@location(0) vec4f {
                let answer=helper();value=answer;return vec4f(f32(answer));}
        ''')
        self.assertIn((17, (5379,)), code)
        extension = next(args for op, args in code if op == 10)
        text = struct.pack('<'+'I'*len(extension), *extension).rstrip(b'\0')
        self.assertEqual(text, b'SPV_EXT_demote_to_helper_invocation')
        demote = next(i for i, (op, _) in enumerate(code) if op == 5380)
        end = next(i for i in range(demote, len(code)) if code[i][0] == 56)
        self.assertTrue(any(op == 62 for op, _ in code[demote+1:end]))
        self.assertTrue(any(op == 254 for op, _ in code[demote+1:end]))
        self.assertFalse(any(op in (252, 4416) for op, _ in code))

    def test_transitive_stage_closure_includes_skipped_and_dead_syntax(self):
        helper = 'fn helper()->bool{discard;return true;}'
        for statement in ['let skipped=false&&helper();', 'return;let dead=helper();',
                          'if(false){let dead=helper();}']:
            self.compile(helper+'@compute @workgroup_size(1) fn main(){'+statement+'}',
                         'discard is restricted to fragment')
        self.compile(helper+'''@vertex fn main()->@builtin(position) vec4f {
            return vec4f(1.0);let dead=helper();}''', 'discard is restricted to fragment')
        self.compile(helper+'''@fragment fn fragment(){let x=helper();}
            @compute @workgroup_size(1) fn compute(){let x=helper();}''',
                     'discard is restricted to fragment')
        self.compile(helper+'@compute @workgroup_size(1) fn main(){}')
        self.compile('@compute @workgroup_size(1) fn main(){return;discard;}',
                     'discard is restricted to fragment')

    def test_discard_is_next_in_behavior_and_uniformity(self):
        self.compile('@fragment fn main()->@location(0) vec4f{discard;}',
                     'must return a value on every path')
        self.compile('@fragment fn main(){loop{discard;}}', 'empty WGSL behavior')
        self.compile('''@fragment fn main(){var n=0u;loop {
            continuing{discard;n+=1u;break if n>2u;}}}''')
        code = self.compile('''enable subgroups;
            @fragment fn main(@builtin(front_facing) face:bool)->@location(0) vec4f {
                if(face){discard;}let x=subgroupAdd(1.0);return vec4f(x);}
        ''')
        self.assertTrue(any(op == 5380 for op, _ in code))
        self.assertTrue(any(op == 350 for op, _ in code))
        self.compile('@fragment fn main(){discard;let invalid=unknown;}', 'name does not resolve')

    def test_ceil_scalar_vector_constants_and_runtime(self):
        code = self.compile('''@fragment fn main()->@location(0) vec4f {
            return vec4f(ceil(1.25),ceil(-1.25),ceil(-0.0f),ceil(-0.25f));}''')
        constants = {args[1]: args[2] for op, args in code if op == 43}
        vectors = [tuple(constants.get(x) for x in args[2:])
                   for op, args in code if op == 44]
        self.assertIn((0x40000000, 0xbf800000, 0x80000000, 0x80000000), vectors)
        code = self.compile('''@fragment fn main(@location(0) input:vec4f)->@location(0) vec4f {
            return ceil(input)+vec4f(ceil(input.x));}''')
        self.assertEqual(sum(op == 12 and args[3] == 9 for op, args in code), 2)
        for value in ['1u', 'vec2i(1)', 'true', 'mat2x2f()']:
            self.compile('@compute @workgroup_size(1) fn main(){let x=ceil('+value+');}',
                         'builtin')
        self.compile('@compute @workgroup_size(1) fn main(){ceil(1.0);}', 'used result')
        self.compile('''fn ceil(x:u32)->u32{return x+1u;}
            @compute @workgroup_size(1) fn main(){let x=ceil(1u);}''')

    def test_retained_gaussian_splat_source(self):
        source = (test_stages.ROOT / 'demos/gaussian-splat-viewer/splat-render.wgsl').read_text()
        code = self.compile(source)
        self.assertEqual([args[0] for op, args in code if op == 15], [0, 4])
        self.assertEqual(sum(op == 5380 for op, _ in code), 2)
        self.assertTrue(any(op == 12 and args[3] == 9 for op, args in code))


if __name__ == '__main__':
    unittest.main()
