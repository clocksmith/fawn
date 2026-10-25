#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Source-bound module results, input normalization, arithmetic and policy failures."""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import unittest

from cases import ROOT, cases


def reversed_fields(value):
    if isinstance(value, dict):
        return {key: reversed_fields(child) for key, child in reversed(list(value.items()))}
    if isinstance(value, list):
        return [reversed_fields(child) for child in value]
    return value


class Modules(unittest.TestCase):
    binary: Path
    work: Path

    def run_case(self, case, request=None, policy=None, ok=True):
        name, module, original_request, original_policy, policy_path = case
        request_path = self.work / 'request.json'
        request_path.write_text(json.dumps(original_request if request is None else request), encoding='utf-8')
        policy_file = self.work / policy_path
        policy_file.parent.mkdir(parents=True, exist_ok=True)
        policy_file.write_text(json.dumps(original_policy if policy is None else policy), encoding='utf-8')
        result = subprocess.run([str(self.binary), '--module', module, '--request', str(request_path), '--policy', policy_path],
                                cwd=self.work, text=True, capture_output=True, timeout=10)
        if not ok:
            self.assertNotEqual(result.returncode, 0, name)
            self.assertEqual(result.stdout, '', name + ': no published result')
            return result
        self.assertEqual(result.returncode, 0, f'{name}: {result.stderr}')
        value = json.loads(result.stdout)
        core = result.stdout[:result.stdout.rfind(',"traceLink":')] + '}'
        self.assertEqual(value['traceLink']['resultHash'], hashlib.sha256(core.encode()).hexdigest(), name)
        if module == 'doe_numeric_stability':
            self.assertGreaterEqual(value['timingStats']['dispatchNs'], 0)
            value['timingStats']['dispatchNs'] = None
            value['traceLink']['resultHash'] = None
        return value

    def test_retained_source_and_contracts(self):
        retained = json.loads((Path(__file__).with_name('retained.json')).read_text())
        for path, digest in retained['sourceSha256'].items():
            self.assertEqual(hashlib.sha256((ROOT / path).read_bytes()).hexdigest(), digest, 'retained source drift: ' + path)
        for case in cases():
            with self.subTest(case=case[0]):
                expected = retained['cases'][case[0]]
                self.assertEqual(self.run_case(case), expected)
                self.assertEqual(self.run_case(case, reversed_fields(case[2]), reversed_fields(case[3])), expected,
                                 'input object order cannot change typed artifact hashes')

    def test_unknown_missing_wrong_types(self):
        for case in cases()[:10]:
            for mutation in ('unknown', 'missing', 'wrong-kind', 'wide-version'):
                r = deepcopy(case[2])
                if mutation == 'unknown': r['ignored'] = True
                if mutation == 'missing': r.pop('artifactKind')
                if mutation == 'wrong-kind': r['artifactKind'] = 'policy'
                if mutation == 'wide-version': r['schemaVersion'] = 2**32
                with self.subTest(module=case[1], mutation=mutation): self.run_case(case, r, ok=False)
        r = deepcopy(cases()[8][2]); r['textRuns'][0]['color'] = [0, 1, 2, 256]
        self.run_case(cases()[8], r, ok=False)
        r['textRuns'][0]['color'] = [0, 1, 2]
        self.run_case(cases()[8], r, ok=False)
        r = deepcopy(cases()[4][2]); r['pathStream'][0]['points'][0] = [1]
        self.run_case(cases()[4], r, ok=False)

    def test_overflow_and_invalid_numeric_shape(self):
        compute = cases()[0]
        for inputs, dispatch in [([{'kind':'a','bytes':2**64-1},{'kind':'b','bytes':1}],dict(x=1,y=1,z=1)),
                                 ([],dict(x=2**32-1,y=2**32-1,z=2)), ([],dict(x=0,y=1,z=1))]:
            r = deepcopy(compute[2]); r.update(inputs=inputs,dispatch=dispatch)
            self.run_case(compute, r, ok=False)
        numeric = next(c for c in cases() if c[0] == 'numeric-divergence')
        for mutation in ('length', 'empty', 'one', 'service', 'nonfinite'):
            r = deepcopy(numeric[2])
            if mutation == 'length': r['candidates'][0]['weights'] = []
            if mutation == 'empty': r['hiddenState'] = []
            if mutation == 'one': r['candidates'] = r['candidates'][:1]
            if mutation == 'service': r['serviceId'] = 'unsupported'
            if mutation == 'nonfinite': r['hiddenState'][0] = 1e300
            self.run_case(numeric, r, ok=False)

    def test_registry_references_and_route_meaning(self):
        numeric = next(c for c in cases() if c[0] == 'numeric-divergence')
        for group, key, bad in [('routeDecisionMetadata','selectionMode','imaginary'),
                                ('routeDecisionMetadata','committedResultMode','imaginary'),
                                ('routeDecisionMetadata','downstreamAction','imaginary'),
                                ('routingPolicies','triggerPolicyId','missing'),
                                ('executionProfiles','routingPolicyId','missing'),
                                ('autoDetectProfiles','triggerPolicyId','missing'),
                                ('autoDetectProfiles','capturePlanId','')]:
            p = deepcopy(numeric[3]); p[group][0][key] = bad
            self.run_case(numeric, policy=p, ok=False)
        p = deepcopy(numeric[3]); p['routeDecisions'].append('without-metadata')
        self.run_case(numeric, policy=p, ok=False)

    def test_receipt_and_meta_publication(self):
        case = next(c for c in cases() if c[0] == 'numeric-divergence')
        request = deepcopy(case[2]); request['receiptPath'] = 'nested/receipt.jsonl'; request['traceMetaPath'] = 'nested/meta.json'
        output = self.run_case(case, request)
        receipt = json.loads((self.work / request['receiptPath']).read_text())
        self.assertEqual(receipt, output['receipt'])
        meta_text = (self.work / request['traceMetaPath']).read_text().strip()
        meta = json.loads(meta_text)
        # The original module directly serializes the summary struct, unlike runtime trace meta's custom writer.
        self.assertEqual(meta['numericStability']['receipt_count'], 1)
        self.assertEqual(meta['numericStability']['decision_counts']['prefer_stable'], 1)
        core = meta_text[:meta_text.index(',"hash":')] + meta_text[meta_text.index(',"numericStability":'):]
        self.assertEqual(meta['hash'], 'sha256:' + hashlib.sha256(core.encode()).hexdigest())
        self.assertEqual(meta['previousHash'], 'sha256:'+'0'*64)
        saved = (self.work / request['receiptPath']).read_bytes()
        request['hiddenState'][0] = 1e300
        self.run_case(case, request, ok=False)
        self.assertEqual((self.work / request['receiptPath']).read_bytes(), saved, 'nonfinite computation must not overwrite receipt')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True, help='C module executable')
    parser.add_argument('--work', type=Path, required=True, help='Isolated artifact directory')
    args = parser.parse_args()
    Modules.binary = args.binary.resolve(); Modules.work = args.work.resolve()
    Modules.work.mkdir(parents=True, exist_ok=True)
    unittest.main(argv=['test_modules.py'], verbosity=2)
