#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fresh emitted HLSL -> official DXC cs_6_0 SPIR-V -> actual CPU Vulkan.

This checks target semantics (clamped storage indices, guarded workgroup
indices) on software Vulkan, not D3D12, Windows SDK/driver behavior, physical
hardware, or cross-device qualification. The runtime size table (b2, space7)
is remapped to set 0, binding 2 because the explicit Vulkan consumer admits
four descriptor sets; the consumer fills it from the actual binding ranges.
"""
import os
from pathlib import Path
import subprocess
import sys


def main():
    out, dxc, icd = (Path(arg).resolve() for arg in sys.argv[1:])
    assert dxc.is_file() and icd.is_file()
    root = Path(__file__).resolve().parents[4]
    subprocess.run([str(out / 'test_hlsl'), *[str(out / (name + '.hlsl')) for name in
                    ['control', 'aggregate', 'effects', 'image_guard', 'graphics',
                     'array_length', 'matrices', 'component_math']], str(out / 'atomic')], check=True)
    backend = root / 'backends/vulkan'
    library = out / 'vulkan'
    subprocess.run(['make', '-C', str(backend), 'OUT=' + str(library), 'all'], check=True)
    consumer = out / 'test_atomic_vulkan'
    subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                    '-I' + str(backend / 'include'), str(Path(__file__).with_name('test_atomic_vulkan.c')),
                    str(library / 'libdoe_vk.a'), '-ldl', '-o', str(consumer)], check=True)
    environment = dict(os.environ, VK_ICD_FILENAMES=str(icd))
    for case in ['signed_storage', 'unsigned_storage', 'signed_shared', 'unsigned_shared', 'effects']:
        source = out / ('atomic_' + case + '.hlsl')
        spirv = out / ('atomic_' + case + '.spv')
        subprocess.run([str(dxc), '-T', 'cs_6_0', '-E', 'main', '-Fo', str(source) + '.dxil', str(source)], check=True)
        subprocess.run([str(dxc), '-T', 'cs_6_0', '-E', 'main', '-spirv', '-fspv-target-env=vulkan1.1',
                        '-fvk-bind-register', 'u0', '0', '0', '0', '-fvk-bind-register', 'u1', '0', '1', '0',
                        '-fvk-bind-register', 'b2', '7', '2', '0', '-Fo', str(spirv), str(source)], check=True)
        subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(spirv)], check=True)
        subprocess.run([str(consumer), str(spirv), case], env=environment, check=True)


if __name__ == '__main__':
    main()
