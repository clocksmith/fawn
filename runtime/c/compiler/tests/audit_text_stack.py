#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Bundled text-target graph with inherited core/target recursion ceilings.

Metal recursive control, constant and type helpers each reject depth >96. Charge
98 visits per family (including the rejecting call) together, not merely the
largest family. This additional target phase is separate from source validation.
All actual normal/PIC .su/.ci records and nm-verified aliases are retained.
External libc vsnprintf is explicitly excluded like the inherited libc leaves.
"""
import argparse
import json
from pathlib import Path
import audit_m13_stack as audit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    arguments = parser.parse_args()
    ceiling, members = audit.FAMILIES['semantic_expression']
    audit.FAMILIES['semantic_expression'] = (
        ceiling, members | {'sema_image_call', 'sema_atomic_call', 'sema_address_reference',
                            'override_integer'})
    # GCC13's actual fortified libc/canary leaves are foreign implementation
    # frames, like memcpy/vsnprintf; do not accept arbitrary unresolved symbols.
    audit.EXTERNAL.update({'vsnprintf', 'feclearexcept', 'fetestexcept',
                           '__memcpy_chk', '__vsnprintf_chk', '__stack_chk_fail'})
    # Actual libm leaves from the declared-width component-math owner. As with
    # inherited exp/sqrt/tanh, their foreign implementation frames are excluded.
    for name in ('acos', 'acosh', 'asin', 'asinh', 'atan', 'atan2', 'atanh', 'cos', 'cosh',
                 'exp2', 'floor', 'fma', 'log', 'log2', 'nearbyint', 'pow', 'sin', 'sinh', 'tan', 'trunc'):
        audit.EXTERNAL.update((name, name + 'f'))
    audit.FAMILIES.update({
        'metal_control': (384, {'metal_block', 'selection', 'short_circuit', 'loop_body'}),
        'metal_constant': (256, {'metal_constant'}),
        'metal_type': (256, {'type_name', 'metal_type', 'metal_handle_type'}),
        # HLSL uses the same existing target control/type ceilings and depth96.
        'hlsl_control': (384, {'hlsl_block', 'selection', 'short_circuit', 'loop_body'}),
        'hlsl_type': (256, {'hlsl_type', 'hlsl_handle_type'}),
    })
    report = audit.audit(arguments.root)
    recursive = sum(row['bytes'] for row in report['sccs'] if row['family'].startswith('metal_'))
    phase = recursive * 98
    report['metal_recursive_visits_per_family'] = 98
    report['metal_recursive_phase'] = phase
    report['metal_composed_bound'] = (phase + report['maximum_acyclic_library_path_bytes'] +
                                     report['wrappers']['doe_compiler_compile_text'])
    # The explicit text adapter fits the existing maximum job budget; source and
    # target phases are disjoint, so add neither recursive phase twice.
    assert report['metal_composed_bound'] <= 196608
    report['maximum_text_job_combined'] = max(report['maximum_job_combined'], report['metal_composed_bound'])
    (arguments.root / 'complete-stack.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print(json.dumps({key: value for key, value in report.items()
                      if key not in {'all_frames', 'ci_sha256', 'su_sha256'}}, indent=2))


if __name__ == '__main__':
    main()
