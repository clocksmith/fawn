# SPDX-License-Identifier: MIT
"""Fixed retained consumers plus independent boundary perturbations."""
from copy import deepcopy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
FIXTURES = ROOT / 'browser/chromium/module-incubation/fixtures'
MODELS = [
    ('fawn_compute_services', 'fawn-compute-services', 'compute-services', 'input-contract-invalid'),
    ('fawn_effects_pipeline', 'fawn-effects-pipeline', 'effects-pipeline', 'unsupported-op'),
    ('fawn_path_engine', 'fawn-path-engine', 'path-engine', 'dash-pattern'),
    ('fawn_resource_scheduler', 'fawn-resource-scheduler', 'resource-scheduler', 'invalid-cadence'),
    ('fawn_2d_sdf_renderer', 'fawn-2d-sdf-renderer', 'sdf-renderer', 'unsupported-sample-count'),
]

def cases():
    result = []
    for module, stem, policy, edge in MODELS:
        path = f'config/{policy}.policy.json'
        request = json.loads((FIXTURES / f'{stem}.request.json').read_text())
        p = json.loads((ROOT / path).read_text())
        result.append((stem, module, request, p, path))
        edge_request = json.loads((FIXTURES / f'{stem}.edge.{edge}.request.json').read_text())
        result.append((stem + '-edge', module, edge_request, p, path))
    module, stem, policy, _ = MODELS[0]
    base = result[0]
    for label, mutate in [
        ('unknown-service', lambda r, p: r.update(serviceId='unknown')),
        ('unknown-kernel', lambda r, p: r.update(kernelId='unknown')),
        ('dispatch-limit', lambda r, p: p['policy'].update(maxDispatchesPerRequest=0)),
        ('failure-precedence', lambda r, p: (r.update(serviceId='unknown'), p['policy'].update(maxInputBytes=0, maxDispatchesPerRequest=0))),
        ('uint64-exact', lambda r, p: r['inputs'][0].update(bytes=9007199254740993)),
        ('string-escapes', lambda r, p: r['inputs'][0].update(kind='zero\x00back\bform\fslash/é')),
        ('integer-coercion', lambda r, p: (r.update(schemaVersion=1.0), r['dispatch'].update(x='2e0', y='1__0'))),
    ]:
        r, p = deepcopy(base[2]), deepcopy(base[3]); mutate(r, p)
        result.append((label, module, r, p, base[4]))
    for index, key, mutate in [
        (2, 'effect-empty', lambda r, p: r.update(effectGraph=[], inputs=[])),
        (2, 'effect-default', lambda r, p: r['effectGraph'][0].pop('params', None)),
        (2, 'effect-all-failures', lambda r, p: p['policy'].update(allowedOps=[], allowedColorSpaces=[], maxNodeCount=0, maxIntermediateBytes=0)),
        (4, 'path-empty', lambda r, p: r.update(pathStream=[])),
        (4, 'float-coercion', lambda r, p: r['strokeState'].update(width='1.25')),
        (4, 'path-all-failures', lambda r, p: (r['strokeState'].update(dashPattern=[1.5]), p['policy'].update(allowedJoinModes=[], allowDashPatterns=False, maxSegments=0, maxTargetPixels=0))),
        (6, 'pool-empty', lambda r, p: r.update(resourceRequest=[])),
        (6, 'pool-truncate', lambda r, p: (r['schedulerPolicy'].update(poolLimitBytes=7), p['policy'].update(maxPoolBytes=5, maxResourcesPerRequest=1))),
        (8, 'sdf-defaults', lambda r, p: (r['textRuns'][0].pop('fontPx', None), r['paintState']['clipState'].pop('bounds', None))),
        (8, 'sdf-all-failures', lambda r, p: p['policy'].update(allowedBlendModes=[], allowedClipModes=[], allowedSampleCounts=[], maxPathOps=0, maxTargetPixels=0, atlasGlyphCapacity=0)),
    ]:
        base = result[index]; r, p = deepcopy(base[2]), deepcopy(base[3]); mutate(r, p)
        result.append((key, base[1], r, p, base[4]))
    r = json.loads((ROOT / 'examples/numeric-stability-service.request.sample.json').read_text())
    r.pop('receiptPath'); r.pop('traceMetaPath')
    p = json.loads((ROOT / 'config/numeric-stability-policy.json').read_text())
    result.append(('numeric-divergence', r['moduleId'], deepcopy(r), deepcopy(p), 'config/numeric-stability-policy.json'))
    for name, hidden, weights in [
        ('tie', [1], [[1], [1]]),
        ('half-rounding', [1, 1, 1], [[1.00048828125, 2**-25, -1], [1.00146484375, 3*2**-25, -1]]),
        ('f32-cancellation', [1, 1, 1], [[1, 2**-25, -1], [0, 2**-26, 0]]),
        ('bias', [1], [[.5], [.25]]),
        ('negative-zero', [-0.0], [[1], [-1]]),
    ]:
        request = deepcopy(r); request['hiddenState'] = hidden
        request['candidates'] = [dict(tokenId=i+10, weights=w) for i, w in enumerate(weights)]
        if name == 'bias': request['candidates'][1]['bias'] = .5
        result.append(('numeric-'+name, request['moduleId'], request, deepcopy(p), 'config/numeric-stability-policy.json'))
    for entry in p['routingPolicies']:
        request = deepcopy(r); request['routingPolicyId'] = entry['policyId']; request['triggerPolicyId'] = entry['triggerPolicyId']
        result.append(('numeric-route-'+entry['policyId'].split('/')[-1], r['moduleId'], request, deepcopy(p), 'config/numeric-stability-policy.json'))
    return result
