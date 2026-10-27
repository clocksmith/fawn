"""CPU controls for prospective native sole-adapter comparison admission."""
from __future__ import annotations

import copy
import ctypes
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from bench.tests import test_package_three_arm as fixtures
from bench.tools import compare_c_vs_zig_package as comparator
from bench.tools import physical_adapter_inventory as inventory


def metal() -> dict:
    return {'backend': 'metal', 'host': 'frozen-host',
            'devices': [{'name': 'Apple M3', 'registryID': 123, 'hardware': True}]}


def meta(name: str, mechanism: str = 'native-adapter-backend-type') -> dict:
    return {'adapterInfo': {'device': name, 'isFallbackAdapter': False},
            'packageBackendSelection': {'requested': 'metal', 'enforced': True,
                                       'mechanism': mechanism,
                                       'observedBackendType': 5 if mechanism.startswith('native') else None}}


class InventoryTests(unittest.TestCase):
    @unittest.skipUnless(platform.system() == 'Linux' and
                         Path('/usr/include/vulkan/vulkan.h').is_file() and
                         shutil.which('cc'), 'requires installed Vulkan C headers')
    def test_vulkan_metadata_mirrors_match_actual_c_header(self) -> None:
        source = '''#include <vulkan/vulkan.h>
#include <stddef.h>
#include <stdio.h>
int main(void) {
_Static_assert(sizeof(VkPhysicalDeviceProperties) <= 4096, "properties capacity");
_Static_assert(_Alignof(VkPhysicalDeviceProperties) <= 8, "properties alignment");
printf("[%zu,%zu,%zu,%zu,%zu,%u,%u]", sizeof(VkApplicationInfo),
sizeof(VkInstanceCreateInfo), sizeof(VkPhysicalDeviceIDProperties),
offsetof(VkPhysicalDeviceProperties2,properties),
offsetof(VkPhysicalDeviceProperties,deviceName),
VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES,
VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
return 0;
}'''
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'probe.c').write_text(source)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(root / 'probe.c'), '-o', str(root / 'probe')],
                           check=True, capture_output=True)
            actual = json.loads(subprocess.check_output([str(root / 'probe')]))
        self.assertEqual(actual, [ctypes.sizeof(inventory._Application),
                                 ctypes.sizeof(inventory._InstanceInfo),
                                 ctypes.sizeof(inventory._DeviceID),
                                 inventory._Properties.properties.offset,
                                 20, 1000071004, 1000059001])

    def test_display_names_alone_stay_incomparable(self) -> None:
        left, right = meta('Apple M3'), meta('apple-m3', 'dawn-create-backend-filter')
        self.assertIn('physical adapter identity differs', comparator.pair_failures(left, right))
        self.assertNotIn('physical adapter identity differs', comparator.pair_failures(
            left, right, single_adapter_inventory=metal()))

    def test_ambiguous_missing_and_unknown_hardware_are_rejected(self) -> None:
        for devices in [[], metal()['devices'] * 2, [{'hardware': True}],
                        [{'name': 'GPU', 'registryID': 0, 'hardware': True}],
                        [*metal()['devices'], {'name': 'virtual', 'hardware': None}]]:
            with self.assertRaises(ValueError):
                inventory.single_identity({**metal(), 'devices': devices})

    def test_sole_hardware_vulkan_ignores_only_classified_cpu(self) -> None:
        device = {'name': 'GPU', 'hardware': True, 'deviceUUID': '1' * 32,
                  'driverUUID': '2' * 32, 'driverVersion': 3}
        value = {'backend': 'vulkan', 'host': 'linux',
                 'devices': [device, {'name': 'llvmpipe', 'hardware': False}]}
        self.assertEqual(inventory.single_identity(value)[0], 'vulkan')
        for key in ('deviceUUID', 'driverUUID'):
            bad = copy.deepcopy(value);bad['devices'][0][key] = '0' * 32
            with self.assertRaises(ValueError):inventory.single_identity(bad)

    def test_native_backend_and_nonfallback_are_required(self) -> None:
        good = meta('GPU')
        for changes in [{'requested': 'vulkan'}, {'enforced': False},
                        {'mechanism': 'assumed'}, {'observedBackendType': 6}]:
            bad = copy.deepcopy(good);bad['packageBackendSelection'].update(changes)
            self.assertFalse(inventory.pair_is_bound(good, bad, metal()))
        bad = copy.deepcopy(good);bad['adapterInfo']['isFallbackAdapter'] = True
        self.assertFalse(inventory.pair_is_bound(good, bad, metal()))

    def test_native_identity_change_invalidates_all_rows(self) -> None:
        changed = copy.deepcopy(metal());changed['devices'][0]['registryID'] += 1
        with tempfile.TemporaryDirectory() as folder, patch.object(
                inventory, 'probe', side_effect=[metal(), changed]) as probe:
            calls, report, code = fixtures.ThreeArmTests().campaign(Path(folder), 'success', single=True)
        self.assertEqual(probe.call_count, 2)
        self.assertEqual(len(calls), 6)
        self.assertEqual(code, 1)
        self.assertTrue(report['drift'])
        self.assertTrue(all(row['status'] == 'incomparable' for row in report['rows']))
        self.assertTrue(all('pairwiseOperationMedianRatios' not in row for row in report['rows']))

    def test_unknown_completion_forbids_post_probe(self) -> None:
        with tempfile.TemporaryDirectory() as folder, patch.object(
                inventory, 'probe', return_value=metal()) as probe:
            _, report, _ = fixtures.ThreeArmTests().campaign(Path(folder), 'unknown', single=True)
        self.assertEqual(probe.call_count, 1)
        self.assertEqual(report['gpuCompletion'], 'unknown')

    def test_ambiguous_inventory_stops_before_any_worker(self) -> None:
        with tempfile.TemporaryDirectory() as folder, patch.object(
                inventory, 'probe', side_effect=ValueError('ambiguous hardware')):
            calls, report, code = fixtures.ThreeArmTests().campaign(Path(folder), 'success', single=True)
        self.assertEqual(calls, [])
        self.assertEqual(code, 1)
        self.assertEqual(report['status'], 'blocked_native_adapter_inventory')
        self.assertEqual(report['gpuCompletion'], 'not_dispatched')


if __name__ == '__main__':
    unittest.main()
