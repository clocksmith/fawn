#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Transparent aliases use the shared hoisted type/constant dependency graph."""
import unittest
import test_stages

ENTRY = '@compute @workgroup_size(1) fn main()'


class TypeAliases(unittest.TestCase):
    compile = test_stages.Stages.compile

    def test_transparent_aggregate_identity_and_forward_dependencies(self):
        code = self.compile('''
            alias Rows = array<Row, COUNT>;
            alias Record = Payload;
            alias Row = vec2<Scalar>;
            alias Scalar = f32;
            const COUNT = u32(Scalar(2));
            struct Payload { rows: Rows }
            alias Result = vec4f;
            fn same(value:Record)->Payload {return value;}
            @fragment fn main()->@location(0) Result {
                let original:Payload=Record(Rows(Row(1,2),vec2f(3,4)));
                let value:Record=same(original);
                return Result(value.rows[0],value.rows[1]);
            }
        ''')
        self.assertEqual(sum(op == 30 for op, _ in code), 1)
        self.assertEqual(sum(op == 23 and a[2] == 2 for op, a in code), 1)

    def test_resource_handles_pointers_and_runtime_array_aliases(self):
        self.compile('''
            alias Element = u32;
            alias Values = array<Element>;
            alias Pointer = ptr<function,Element>;
            alias Texture = texture_2d<f32>;
            alias Sample = sampler;
            @group(0) @binding(0) var<storage,read_write> data:Values;
            @group(0) @binding(1) var image:Texture;
            @group(0) @binding(2) var sample:Sample;
            fn update(value:Pointer){*value=*value+1u;}
            @compute @workgroup_size(1) fn main(){var local=data[0];update(&local);data[1]=local;}
        ''')

    def test_aliases_shadow_builtins_and_lexical_values_shadow_aliases(self):
        code = self.compile('alias sin=u32; alias vec2=f32;' + ENTRY +
                            '{let a=sin(7);let b=vec2(1);}')
        self.assertFalse(any(op == 12 for op, _ in code))
        self.compile('alias Scalar=f32;' + ENTRY +
                     '{let Scalar=1.0;let value=Scalar(2.0);}', 'lexical value')
        self.compile('alias Scalar=f32;' + ENTRY +
                     '{let Scalar=1.0;let value:Scalar=2.0;}', 'lexical value')
        self.compile('alias Scalar=f32;' + ENTRY + '{let value=Scalar;}',
                     'name does not resolve')
        self.compile('alias Vector=vec2f;' + ENTRY + '{let value=Vector<f32>(1);}',
                     'cannot have type arguments')
        self.compile('alias Vector=vec2f;alias Invalid=Vector<f32>;' + ENTRY + '{}',
                     'type generator is shadowed')

    def test_cycles_invalid_unused_aliases_and_one_module_namespace(self):
        for declarations in [
            'alias A=A;',
            'alias A=B;alias B=A;',
            'alias A=array<B,2>;struct B{value:A}',
            'alias A=array<f32,COUNT>;const COUNT=A()[0];',
        ]:
            self.compile(declarations + ENTRY + '{}', 'dependency cycle')
        self.compile('alias Unused=Missing;' + ENTRY + '{}', 'unsupported type name')
        self.compile('alias Unused=array<f32,0>;' + ENTRY + '{}', 'must be positive')
        for declarations in [
            'alias Name=f32;alias Name=u32;',
            'alias Name=f32;const Name=1;',
            'alias Name=f32;struct Name{x:f32}',
            'alias Name=f32;fn Name(){}',
            'alias Name=f32;var<workgroup> Name:u32;',
        ]:
            self.compile(declarations + ENTRY + '{}', 'duplicate module declaration')

    def test_long_forward_chain_is_iterative(self):
        chain = ''.join(f'alias Type{i}=Type{i+1};' for i in range(256))
        self.compile(chain + 'alias Type256=vec4f;' +
                     '@fragment fn main()->@location(0) Type0{return Type0(1);}')
        self.compile(chain + 'alias Type256=Type0;' + ENTRY + '{}', 'dependency cycle')


if __name__ == '__main__':
    unittest.main()
