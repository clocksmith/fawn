"""CPU controls for optional three-provider package measurement admission."""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from bench.tools import compare_c_vs_zig_package as owner
from bench.tests.test_c_vs_zig_package import valid_meta


class ThreeArmTests(unittest.TestCase):
    def test_balanced_order_and_unchanged_two_arm_schedule(self) -> None:
        for row in range(3):
            orders = [owner.provider_order(row, sample, True, 1) for sample in range(1, 17)]
            self.assertEqual(len(set(orders)), 6)
            for arm in ('c', 'zig', 'dawn'):
                counts = [sum(order[position] == arm for order in orders) for position in range(3)]
                self.assertLessEqual(max(counts) - min(counts), 1)
        self.assertEqual(owner.provider_order(0, 0, False), ('c', 'zig'))
        self.assertEqual(owner.provider_order(1, 0, False), ('zig', 'c'))

    def test_three_arm_rejects_other_package_hosts_before_inputs_or_gpu(self) -> None:
        for profile in ['node-direct', 'bun-full', 'node-direct-prepared', 'bun-full-prepared']:
            with self.assertRaisesRegex(ValueError, 'canonical Node package profiles'):
                owner.run(argparse.Namespace(iterations=1, warmup=0, include_dawn=True,
                    backend='metal', profiles=[profile]))

    def test_dawn_provenance_backend_and_full_output_are_required(self) -> None:
        meta = valid_meta()
        inputs = {'/exact/vendor/index.js': 'a' * 64, '/exact/vendor/dawn.node': 'b' * 64}
        meta['packageProviderIdentity'] = {'provider': 'dawn-node-webgpu', 'version': '0.4.0',
            'loaded': True, 'loadedAddon': True, 'importFallback': False, 'inputHashes': inputs}
        meta['packageBackendSelection'] = {'requested': 'metal', 'enforced': True,
            'mechanism': 'dawn-create-backend-filter', 'observedBackendType': None}
        meta['gpuCompletion'] = 'completed'
        def failures(value):
            return owner.sample_failures(value, Path(), 'dawn', False, 4, '1' * 64,
                                         dawn_inputs=inputs, backend='metal')
        self.assertEqual(failures(meta), [])
        for key, value in [('loadedAddon', False), ('importFallback', True), ('version', 'other'),
                           ('inputHashes', {'/wrong/addon': 'b' * 64})]:
            bad = copy.deepcopy(meta);bad['packageProviderIdentity'][key] = value
            self.assertTrue(failures(bad), key)
        for key in ['packageProviderIdentity', 'readbackCaptures', 'packageBackendSelection', 'gpuCompletion']:
            bad = copy.deepcopy(meta);del bad[key]
            self.assertTrue(failures(bad), key)
        for mutation in [{'readbackCaptures': [{'byteLength': 4, 'sha256': '2' * 64}]},
                         {'gpuCompletion': 'unknown'}, {'adapterInfo': {'device': 'CPU', 'isFallbackAdapter': True}}]:
            self.assertTrue(failures({**meta, **mutation}))
        bad = copy.deepcopy(meta);bad['packageBackendSelection']['observedBackendType'] = 6
        self.assertTrue(failures(bad))

    def test_application_replacement_allows_paths_but_not_work_adapter_or_completion_differences(self) -> None:
        left = valid_meta();right = {**left, 'packageReadbackActualPaths': ['standard-mapped-copy']}
        self.assertTrue(owner.pair_failures(left, right))
        self.assertEqual(owner.pair_failures(left, right, application_replacement=True), [])
        for key, value in [('planHash', 'different'), ('executionSubmitCount', 0),
                           ('adapterInfo', {'device': 'different GPU'})]:
            self.assertTrue(owner.pair_failures(left, {**right, key: value}, application_replacement=True))

    def campaign(self, root: Path, mode: str):
        root = root.resolve()
        c = root/'c.dylib';z = root/'zig.dylib';d = root/'dawn.node'
        for path in [c,z,d]:path.write_bytes(path.name.encode())
        build = root/'build.json';build.write_text(json.dumps({'artifacts': {
            str(path): {'sha256': owner.digest(path)} for path in [c,z]}}))
        oracle = root/'oracle';oracle.mkdir();(oracle/'oracle-manifest.json').write_text(json.dumps({
            'generatorSha256': owner.digest(owner.ROOT/'bench/tools/prepare_package_oracle_plans.py')}))
        plan = root/'plan.json';plan.write_text('{}')
        cases = [{'id': 'fixture'+str(i), 'profile': 'node-package', 'host': 'node', 'provider': 'doe',
                  'prepared': False, 'plan': str(plan), 'catalog': str(plan), 'fullOutputBytes': 4,
                  'expectedSha256': '1'*64} for i in range(2)]
        args = argparse.Namespace(iterations=1, warmup=0, timeout=1, include_dawn=True, backend='metal',
            profiles=['node-package'], c_library=c, zig_library=z, build_inputs=build, oracle_plans=oracle, out=root/'out')
        calls=[]
        def sample(command, folder, timeout, **kwargs):
            report=json.loads((args.out/'report.json').read_text())
            self.assertEqual(report['gpuCompletion'], 'unknown')
            self.assertEqual(report['activeWorker']['command'], command)
            self.assertEqual(len(report['rows']), 1 if len(calls)<3 else 2)
            if len(calls)>=3:self.assertEqual(report['rows'][0]['status'],
                'incomparable' if mode=='bad-output' else 'comparable_public_api_work')
            arm=report['activeWorker']['arm']
            self.assertEqual(command[command.index('--backend')+1], 'metal')
            self.assertEqual('--require-vendored-dawn' in command, arm=='dawn')
            calls.append(command)
            if mode=='abrupt' and len(calls)==4:raise SystemExit(77)
            folder.mkdir(parents=True)
            meta=valid_meta();arm=report['activeWorker']['arm']
            meta['packageProviderIdentity'].update(doeLibraryPath=str(c if arm=='c' else z),
                libraryFlavor='doe-c' if arm=='c' else 'doe-dropin')
            meta['packageBackendSelection']={'requested':'metal','enforced':True,
                'mechanism':'native-adapter-backend-type','observedBackendType':5}
            if arm=='dawn':
                meta['packageProviderIdentity']={'provider':'dawn-node-webgpu','version':'0.4.0',
                    'loaded':True,'loadedAddon':True,'importFallback':False,'inputHashes':{str(d):owner.digest(d)}}
                meta['packageBackendSelection'].update(mechanism='dawn-create-backend-filter',observedBackendType=None)
            meta['gpuCompletion']='unknown' if mode=='unknown' and len(calls)==4 else 'completed'
            if mode=='bad-output' and len(calls)==1:meta['readbackCaptures'][0]['sha256']='2'*64
            (folder/'meta.json').write_text(json.dumps(meta));(folder/'trace.jsonl').write_text('{}\n')
            return {'exitCode':0,'timedOut':False,'wallNs':1000000,'command':command}
        with patch.object(owner,'cases',return_value=cases),patch.object(owner,'dawn_input_paths',return_value={d}),\
             patch.object(owner.synthetic_assets,'describe_plan_assets',return_value=[]),\
             patch.object(owner.shutil,'which',return_value=sys.executable),patch.object(owner,'process_sample',side_effect=sample):
            if mode=='abrupt':
                with self.assertRaises(SystemExit):owner.run(args)
            else:code=owner.run(args)
        report=json.loads((args.out/'report.json').read_text())
        return calls,report,None if mode=='abrupt' else code

    def test_successful_close_cannot_erase_unknown_or_launch_later_provider(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            calls,report,code=self.campaign(Path(root),'unknown')
            self.assertEqual(len(calls),4);self.assertEqual(code,1)
            self.assertEqual(report['gpuCompletion'],'unknown')
            self.assertEqual(report['status'],'stopped_gpu_completion_unknown')
            self.assertEqual(report['rows'][0]['status'],'comparable_public_api_work')

    def test_abrupt_second_row_leaves_unknown_checkpoint_and_first_row(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            calls,report,_=self.campaign(Path(root),'abrupt')
            self.assertEqual(len(calls),4);self.assertEqual(report['gpuCompletion'],'unknown')
            self.assertIn('activeWorker',report)
            self.assertEqual(report['rows'][0]['status'],'comparable_public_api_work')

    def test_known_completed_bad_output_is_a_result_and_next_rows_run(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            calls,report,code=self.campaign(Path(root),'bad-output')
            self.assertEqual(len(calls),6);self.assertEqual(code,1)
            self.assertEqual(report['gpuCompletion'],'completed')
            self.assertEqual(report['rows'][0]['status'],'incomparable')
            self.assertEqual(report['rows'][1]['status'],'comparable_public_api_work')


if __name__ == '__main__':unittest.main()
