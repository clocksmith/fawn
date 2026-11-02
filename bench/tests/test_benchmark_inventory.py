"""Inventory regression controls at evidence, identity and selection boundaries."""
from __future__ import annotations

import argparse
from contextlib import redirect_stdout
from copy import deepcopy
import io
import gzip
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import jsonschema

from bench.tools import build_benchmark_cube as builder
from bench.lib import benchmark_cube_reports as reports
from bench.lib import benchmark_inventory_sources as sources
from bench.lib.benchmark_cube_dashboard_html import build_dashboard_html
from bench.tools.native_catalog_report import is_loss, load_policy

ROOT = Path(__file__).resolve().parents[2]
POLICY = json.loads((ROOT / 'config/benchmark-cube-policy.json').read_text())['inventory']


def registration(identifier: str = 'run', path: str | None = None) -> dict:
    return {'id': identifier, 'familyId': 'fixture', 'section': 'application',
            'comparisonType': 'application-provider', 'campaignId': 'campaign',
            'adapter': 'application', 'adapterVersion': 1, 'path': path,
            'sha256': None, 'provenance': [], 'limitations': [], 'supersedes': []}


def observation(identifier: str, date: str | None, *, machine: str | None = 'host-a',
                commit: str = 'a' * 40, qualified: bool = False) -> dict:
    entry = registration(identifier)
    row = reports.inventory_row(entry, 'workload', identifier)
    row.update(executedAt=date, executionTimeSource='receipt#/finishedAt' if date else None,
               workloadContractHash='c' * 64, executionStatus='succeeded')
    row['machine'].update(id=machine, os='linux', gpu='same-model', driver='v1')
    row['implementation'].update(name='doe', sourceCommit=commit, binaryHash='a' * 64)
    row['comparator'].update(name='dawn', sourceCommit='d' * 40, binaryHash='d' * 64)
    row['evidence'] = [{'role': 'report', 'path': 'receipt.json', 'sha256': 'f' * 64,
                        'required': True, 'availability': 'retained'}]
    row['metrics'] = [sources.metric('operation', 'ms', 1, 2, .5, outcome='win')]
    if qualified:
        row['qualification'].update(status='qualified', scope='application', reason='bound receipt')
        for key, status in [('correctness', 'passed'), ('acceptance', 'passed'), ('fallback', 'disabled')]:
            row[key] = {'status': status, 'evidence': 'receipt.json#' + key}
    return reports.finalize_inventory_row(row, POLICY['grouping'])


class TestBenchmarkInventory(unittest.TestCase):
    def test_clocks_are_separate_and_unknown_dates_stay_unknown(self) -> None:
        times = reports.execution_times({'generatedAt': '2026-11-01T03:00:00Z',
                                         'frozenAtUtc': '2026-10-01T00:00:00Z'})
        self.assertIsNone(times['executedAt'])
        self.assertEqual(times['reportedAt'], '2026-11-01T03:00:00Z')
        self.assertEqual(times['contractFrozenAt'], '2026-10-01T00:00:00Z')
        self.assertIsNone(reports.parse_utc_iso('2026-11-01'))
        dated = observation('old', '2026-10-01T00:00:00Z')
        undated = observation('undated', None)
        selected = reports.select_inventory_current([dated, undated], 'a' * 40)[0]
        self.assertEqual(selected['latestMeasured'], dated['rowId'])
        self.assertEqual(selected['undated'], [undated['rowId']])

    def test_touching_report_cannot_create_execution_or_report_time(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / '20261101T123456Z.json'
            path.write_text('{}')
            before = reports.parse_report_timestamp({}, path)
            os.utime(path, (1999999999, 1999999999))
            self.assertIsNone(before)
            self.assertIsNone(reports.parse_report_timestamp({}, path))
            self.assertIsNone(reports.execution_times({})['executedAt'])

    def test_same_hardware_profile_does_not_merge_actual_machines(self) -> None:
        a = observation('a', None, machine='host-a')
        b = observation('b', None, machine='host-b')
        unknown_a = observation('c', None, machine=None)
        unknown_b = observation('d', None, machine=None)
        self.assertEqual(len({r['seriesKey'] for r in [a, b, unknown_a, unknown_b]}), 4)
        self.assertFalse(reports.inventory_qualified(unknown_a))

    def test_provenance_path_does_not_split_known_machine(self) -> None:
        a = observation('a', None)
        b = observation('b', None)
        b['machine']['identitySource'] = 'different-receipt#hostname'
        reports.finalize_inventory_row(b, POLICY['grouping'])
        self.assertEqual(a['seriesKey'], b['seriesKey'])

    def test_new_diagnostic_loss_remains_visible_beside_old_qualified_win(self) -> None:
        old = observation('old', '2026-10-01T00:00:00Z', qualified=True)
        new = observation('new', '2026-11-01T00:00:00Z', commit='b' * 40)
        new['metrics'][0]['outcome'] = 'loss'
        new['executionStatus'] = 'failed'
        result = reports.select_inventory_current([old, new], 'b' * 40)[0]
        self.assertEqual(result['latestMeasured'], new['rowId'])
        self.assertEqual(result['latestQualified'], old['rowId'])
        self.assertEqual(len(result['observations']), 2)

    def test_legacy_cube_cannot_rank_status_row_count_or_report_date_above_execution(self) -> None:
        policy = reports.load_policy(ROOT, ROOT / 'config/benchmark-cube-policy.json')
        lanes = reports.load_governed_lanes(ROOT, ROOT / 'config/governed-lanes.json')
        surface = next(s for s in policy['raw']['surfaces'] if s['id'] == 'backend_native')
        pair = builder.surface_comparison_views(policy, surface)[0]
        old = {'surface': surface['id'], 'providerPair': pair,
               'hostProfile': surface['expectedHostProfiles'][0], 'rowCount': 999,
               'runId': 'old', 'executedAt': '2026-10-01T00:00:00Z',
               'generatedAt': '2026-12-01T00:00:00Z', 'sourceReportPath': 'old.json',
               'claimStatus': 'claimable', 'comparisonStatus': 'comparable',
               'sourceConformance': 'canonical', 'deltaP50MedianPercent': 99}
        new = old | {'rowCount': 1, 'runId': 'new', 'executedAt': '2026-11-01T00:00:00Z',
                     'generatedAt': '2026-11-01T00:00:00Z', 'sourceReportPath': 'new.json',
                     'claimStatus': 'diagnostic', 'comparisonStatus': 'diagnostic',
                     'sourceConformance': 'legacy_nonconformant', 'deltaP50MedianPercent': -50}
        cells = builder.build_cells(policy=policy, governed_lanes=lanes, rows=[], reports=[old, new])
        cell = next(c for c in cells if c['surface'] == surface['id'] and
                    c['providerPair'] == pair and c['hostProfile'] == old['hostProfile'] and
                    c['workloadSet'] == 'full_comparable')
        self.assertEqual(cell['latestReportPath'], 'new.json')

    def test_new_commit_cannot_inherit_old_binary_win(self) -> None:
        old = observation('old', '2026-10-01T00:00:00Z', qualified=True)
        result = reports.select_inventory_current([old], 'b' * 40)[0]
        self.assertFalse(result['currentSourceMeasured'])
        newer = observation('new', '2026-11-01T00:00:00Z', commit='b' * 40)
        newer['implementation']['binaryHash'] = None
        result = reports.select_inventory_current([old, newer], 'b' * 40)[0]
        self.assertFalse(result['currentSourceMeasured'])

    def test_artifact_identity_includes_binary_source_and_compiler(self) -> None:
        base = observation('a', None)
        artifacts = {base['artifactKey']}
        for key, value in [('binaryHash', 'b' * 64), ('sourceCommit', 'b' * 40),
                           ('componentHashes', {'compiler': 'e' * 64})]:
            changed = deepcopy(base)
            changed['implementation'][key] = value
            reports.finalize_inventory_row(changed, POLICY['grouping'])
            self.assertEqual(base['seriesKey'], changed['seriesKey'])
            artifacts.add(changed['artifactKey'])
        self.assertEqual(len(artifacts), 4)

    def test_browser_and_workspace_cannot_qualify_native_runtime(self) -> None:
        for comparison in ('browser-provider', 'workspace-behavior'):
            row = observation(comparison, '2026-11-01T00:00:00Z', qualified=True)
            row['section'] = 'native-runtime'
            row['qualification']['scope'] = 'native-runtime'
            row['comparisonType'] = comparison
            self.assertFalse(reports.inventory_qualified(row))
        entry = registration('browser')
        entry.update(adapter='browser-fresh-launch', section='native-runtime')
        with self.assertRaisesRegex(ValueError, 'evidence surface'):
            sources.normalize_source(ROOT, entry, POLICY)

    def test_units_validate_and_remain_distinct(self) -> None:
        schema = json.loads((ROOT / 'config/benchmark-cube-row.schema.json').read_text())
        validator = jsonschema.Draft202012Validator(schema)
        row = observation('units', None)
        for unit in ('ms', 'ns', 'MiB', 'bytes', 'count', 'boolean', 'ratio', None):
            row['metrics'] = [sources.metric('value', unit, 1)]
            validator.validate(row)
        row['metrics'][0]['unit'] = 'seconds-probably'
        with self.assertRaises(jsonschema.ValidationError):
            validator.validate(row)
        self.assertEqual(sources.phase_unit('maxRssMiB'), 'MiB')
        self.assertEqual(sources.phase_unit('frameworkImagesBeforeSetup'), 'count')

    def test_catalog_uses_absolute_doe_floor_not_difference(self) -> None:
        policy = load_policy(ROOT / POLICY['catalogClassification']['path'])
        # Doe=.06, Dawn=.055: difference <.01, but actual absolute floor passes.
        self.assertTrue(is_loss('warm.operation_ms', {'p50': .06 / .055, 'ci95': [1.01, 1.2]},
                                {'p50': .06}, policy))
        self.assertFalse(is_loss('warm.operation_ms', {'p50': 2, 'ci95': [1.5, 2.5]},
                                 {'p50': .04}, policy))

    def test_supersession_preserves_history_and_checks_references(self) -> None:
        a = observation('a', '2026-11-01T00:00:00Z')
        b = observation('b', '2026-11-01T00:00:00Z')
        b['supersedes'] = ['a']
        result = reports.select_inventory_current([a, b], 'a' * 40)[0]
        self.assertEqual(result['latestMeasured'], b['rowId'])
        self.assertEqual(len(result['observations']), 2)
        for references in (['absent'], ['b']):
            b['supersedes'] = references
            with self.assertRaises(ValueError):
                reports.select_inventory_current([a, b], 'a' * 40)
        a['supersedes'], b['supersedes'] = [b['rowId']], [a['rowId']]
        with self.assertRaisesRegex(ValueError, 'Cyclic'):
            reports.select_inventory_current([a, b], 'a' * 40)

    def test_missing_and_ephemeral_evidence_cannot_qualify(self) -> None:
        entry = registration('missing', 'not-present.json')
        rows = sources.normalize_source(ROOT, entry, POLICY)
        self.assertEqual(rows[0]['evidence'][0]['availability'], 'missing')
        self.assertIsNone(rows[0]['executedAt'])
        for availability in ('missing', 'ephemeral'):
            row = observation(availability, '2026-11-01T00:00:00Z', qualified=True)
            row['evidence'][0]['availability'] = availability
            self.assertFalse(reports.inventory_qualified(row))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'receipt.json'
            path.write_text('{}')
            ref = {'role': 'report', 'path': path.name, 'sha256': '0' * 64, 'required': True}
            with self.assertRaisesRegex(ValueError, 'checksum'):
                sources.evidence_reference(root, ref)
            ref['sha256'] = sources.file_hash(path)
            with patch.object(sources.subprocess, 'run') as run:
                run.return_value.returncode = 0
                self.assertEqual(sources.evidence_reference(root, ref)['availability'], 'ephemeral')

    def test_application_retains_failed_processes_and_native_units(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'application.json'
            path.write_text(json.dumps({'rows': [
                {'arm': 'doe', 'stage': 'warm', 'exitCode': 2, 'processWallNs': 120, 'peakRssBytes': 40},
                {'arm': 'dawn', 'stage': 'warm', 'exitCode': 0, 'runWallNs': [10, 20]}]}))
            entry = registration('app', path.name)
            entry['sha256'] = sources.file_hash(path)
            rows = sources.normalize_source(root, entry, POLICY)
            self.assertEqual([r['executionStatus'] for r in rows[1:]], ['failed', 'succeeded'])
            self.assertEqual([m['unit'] for m in rows[1]['metrics']], ['ns', 'bytes'])
            self.assertEqual([m['implementationValue'] for m in rows[2]['metrics']], [10, 20])

    def test_phase_failures_keep_individual_clocks_and_contract_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'summary.json').write_text('{"workloads": {}}')
            (root / 'contract.json').write_text('{"frozenAtUtc": "2026-10-01T00:00:00Z"}')
            (root / 'gate.json').write_text('{"after": {"utc": "2026-11-01T00:00:00Z"}}')
            with gzip.open(root / 'raw.jsonl.gz', 'wt') as stream:
                for _ in range(2):
                    stream.write(json.dumps({'workload': 'image', 'provider': 'doe',
                                             'returnCode': 1, 'result': {'status': 'failed'}}) + '\n')
            entry = registration('phase', 'summary.json')
            entry.update(adapter='phase-cohort', section='native-runtime', comparisonType='native-provider')
            entry['sha256'] = sources.file_hash(root / entry['path'])
            for role, path in [('contract', 'contract.json'), ('execution', 'gate.json'), ('raw', 'raw.jsonl.gz')]:
                entry['provenance'].append({'role': role, 'path': path,
                    'sha256': sources.file_hash(root / path), 'required': True})
            rows = sources.normalize_source(root, entry, POLICY)
            self.assertEqual(len(rows), 2)
            self.assertEqual(len({r['rowId'] for r in rows}), 2)
            self.assertEqual(len({r['machine']['id'] for r in rows}), 2)
            for row in rows:
                self.assertEqual(row['executionStatus'], 'failed')
                self.assertIsNone(row['executedAt'])  # Window end cannot date each process.
                self.assertEqual(row['contractFrozenAt'], '2026-10-01T00:00:00Z')

    def test_untracked_receipt_is_not_a_durable_public_reference(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '--quiet', str(root)], check=True)
            path = root / 'receipt.json'
            path.write_text('{}')
            ref = {'role': 'report', 'path': path.name, 'sha256': sources.file_hash(path), 'required': True}
            self.assertEqual(sources.evidence_reference(root, ref)['availability'], 'ephemeral')
            subprocess.run(['git', '-C', str(root), 'add', path.name], check=True)
            self.assertEqual(sources.evidence_reference(root, ref)['availability'], 'retained')

    def test_generation_check_duplicates_and_dashboard_share_selection(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'config').mkdir()
            for name in ('benchmark-cube-row.schema.json', 'benchmark-cube.schema.json',
                         'benchmark-cube-policy.json'):
                shutil.copyfile(ROOT / 'config' / name, root / 'config' / name)
            path = root / 'receipt.json'
            path.write_text(json.dumps({'generatedAt': '2026-11-01T00:00:00Z', 'primaryRatio': .5}))
            entry = registration('receipt', path.name)
            entry['sha256'] = sources.file_hash(path)
            registered = {'schemaVersion': 1, 'families': [
                {'id': 'fixture', 'label': 'Fixture', 'section': 'application', 'expectedOs': ['linux']}],
                'sources': [entry]}
            source_path = root / 'sources.json'
            source_path.write_text(json.dumps(registered))
            args = argparse.Namespace(policy='config/benchmark-cube-policy.json', sources='sources.json',
                index_out='index.json', current_out='CURRENT.md', source_ref='HEAD', check=False,
                dashboard_html_out='')
            with patch.object(builder, 'load_policy', return_value={'raw': {'inventory': POLICY}}), \
                 patch('subprocess.check_output', return_value='b' * 40), redirect_stdout(io.StringIO()):
                payload = builder.build_inventory(args, root)
                before = (root / 'index.json').read_bytes()
                builder.build_inventory(args, root)
                self.assertEqual(before, (root / 'index.json').read_bytes())
                os.utime(path, (1999999999, 1999999999))
                args.check = True
                builder.build_inventory(args, root)
                history = root / 'families/fixture.md'
                history.write_text('stale generated page')
                with self.assertRaisesRegex(ValueError, 'Stale benchmark inventory'):
                    builder.build_inventory(args, root)
                args.check = False
                builder.build_inventory(args, root)
                args.check = True
                builder.build_inventory(args, root)
                orphan = root / 'families/removed.md'
                orphan.write_text('<!-- Generated by benchmark cube; do not edit. -->')
                with self.assertRaisesRegex(ValueError, 'Stale benchmark inventory'):
                    builder.build_inventory(args, root)
                args.check = False
                builder.build_inventory(args, root)
                self.assertFalse(orphan.exists())
                args.check = True
                self.assertIsNone(payload['current'][0]['latestMeasured'])
                invalid = deepcopy(payload)
                invalid['rows'][0]['metrics'][0]['unit'] = 'made-up'
                with self.assertRaises(ValueError):
                    reports.validate_schema(root / 'config/benchmark-cube.schema.json', invalid)
                html = build_dashboard_html(payload, {}, dashboard_path=root / 'dashboard.html')
                self.assertIn('current source unmeasured', html)
                (root / 'CURRENT.md').write_text('stale')
                with self.assertRaisesRegex(ValueError, 'Stale'):
                    builder.build_inventory(args, root)
                args.check = False
                for duplicate in (deepcopy(entry), dict(entry, id='alias', path='./receipt.json')):
                    registered['sources'] = [entry, duplicate]
                    source_path.write_text(json.dumps(registered))
                    with self.assertRaisesRegex(ValueError, 'Duplicate'):
                        builder.build_inventory(args, root)


if __name__ == '__main__':
    unittest.main()
