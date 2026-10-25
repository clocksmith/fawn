# SPDX-License-Identifier: MIT
"""WGSL 15.2.7 parameter-return dependency controls, not GPU qualification.

The reduction's NoRestriction function tag does not remove its default
ParameterReturnContentsRequiredToBeUniform parameter tag. Keep the original
negative test_subgroups.py fixture unchanged; exercise transitive/all-target
paths here. Normative source: https://www.w3.org/TR/2026/CRD-WGSL-20261021/
"""
from pathlib import Path
import tempfile
from test_native_builtins import compile_shader

PREFIX = 'enable subgroups; requires subgroup_uniformity;'
ENTRY = '@compute @workgroup_size(8) fn main(@builtin(local_invocation_index)lane:u32)'
HELPER = 'fn reduced(x:u32)->u32{return subgroupAdd(x);}'
RELAY = HELPER + 'fn relay(x:u32)->u32{return reduced(x);}'


def main():
    with tempfile.TemporaryDirectory(prefix='doe-subgroup-return-') as temporary:
        directory = Path(temporary)
        for name, helpers, expression in [
                ('direct', '', 'subgroupAdd(lane)'),
                ('helper', HELPER, 'reduced(lane)'),
                ('relay', RELAY, 'relay(lane)'),
                ('broadcast', '', 'subgroupBroadcast(lane,0u)'),
                ('minimum', '', 'subgroupMin(lane)')]:
            for target in ('spirv', 'msl', 'hlsl'):
                key = name + '_' + target
                source = PREFIX + helpers + ENTRY + '{if ' + expression + '>0u {let x=subgroupAdd(2u);}}'
                compile_shader(directory, key + '_varying', source, target, False)
                compile_shader(directory, key + '_uniform', source.replace('(lane', '(1u'), target)
                # Varying arguments remain legal when their result does not
                # become proof for a later collective's control flow.
                source = PREFIX + helpers + ENTRY + '{let x=' + expression + ';}'
                compile_shader(directory, key + '_value', source, target)
    print('subgroup argument-return dependencies: direct/transitive and three targets passed')


if __name__ == '__main__':
    main()
