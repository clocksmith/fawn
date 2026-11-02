"""Receipt adapters for the existing benchmark cube inventory mode.

Adapters expose recorded observations, never recover missing samples, infer
execution from report dates, or promote a diagnostic result into qualification.
"""
from __future__ import annotations

from dataclasses import dataclass, field
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
from typing import Any, Iterator

from bench.lib import benchmark_cube_reports as reports
from bench.tools import native_catalog_report as catalog_classifier


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_payload(path: Path) -> Any:
    if path.name.endswith('.gz'):
        with gzip.open(path, 'rt', encoding='utf-8') as stream:
            return json.load(stream)
    return json.loads(path.read_text(encoding='utf-8'))


@dataclass
class Source:
    root: Path
    registration: dict[str, Any]
    references: list[dict[str, Any]]
    payload: dict[str, Any]
    policy: dict[str, Any]
    cache: dict[str, Any] = field(default_factory=dict)

    def reference(self, role: str) -> dict[str, Any] | None:
        return next((r for r in self.references if r['role'] == role), None)

    def data(self, role: str) -> Any:
        if role in self.cache:
            return self.cache[role]
        ref = self.reference(role)
        if ref is None or ref['availability'] != 'retained':
            return None
        path = self.root / ref['path']
        if path.suffix in ('.txt', '.md'):
            self.cache[role] = path.read_text(encoding='utf-8')
        else:
            self.cache[role] = read_payload(path)
        return self.cache[role]

    def row(self, workload: str, execution: str = 'summary',
            level: str = 'aggregate') -> dict[str, Any]:
        row = reports.inventory_row(self.registration, workload,
                                    self.registration['id'] + '/' + workload + '/' + execution,
                                    level=level)
        row['evidence'] = self.references.copy()
        row.update(reports.execution_times(self.payload))
        row['limitations'].extend(self.registration.get('limitations', []))
        for ref in self.references:
            if ref['availability'] != 'retained':
                row['limitations'].append(ref['role'] + ' evidence ' + ref['availability'] + ': ' + str(ref['path']))
        return row


def evidence_reference(root: Path, reference: dict[str, Any]) -> dict[str, Any]:
    """Verify exact bytes and prohibit ephemeral output as durable evidence."""
    result = {**reference, 'availability': 'missing'}
    if reference['path'] is None:
        return result
    path = root / reference['path']
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError('Evidence outside repository: ' + str(path))
    if not path.is_file():
        return result
    actual = file_hash(path)
    if actual != reference['sha256']:
        raise ValueError('Evidence checksum mismatch: ' + reference['path'])
    ignored = subprocess.run(['git', '-C', str(root), 'check-ignore', '--quiet',
                              reference['path']], capture_output=True).returncode == 0
    tracked = subprocess.run(['git', '-C', str(root), 'ls-files', '--error-unmatch',
                              '--', reference['path']], capture_output=True).returncode
    # Return 128 permits standalone fixture roots outside Git. In the actual
    # repository, an untracked file is not durable until retained in the index.
    result['availability'] = 'ephemeral' if ignored or tracked == 1 else 'retained'
    return result


def make_source(root: Path, registration: dict[str, Any],
                policy: dict[str, Any]) -> Source:
    refs = [evidence_reference(root, {'role': 'report', 'path': registration['path'],
                                    'sha256': registration['sha256'], 'required': True})]
    refs += [evidence_reference(root, r) for r in registration.get('provenance', [])]
    payload = read_payload(root / registration['path']) if refs[0]['availability'] == 'retained' else {}
    return Source(root, registration, refs, payload, policy)


def metric(name: str, unit: str | None, value: Any = None,
           comparator: Any = None, ratio: Any = None, *,
           scope: str = 'reported aggregate', count: int | None = None,
           ci: list[float] | None = None, outcome: str = 'unknown',
           policy: dict[str, Any] | None = None) -> dict[str, Any]:
    record = {'name': name, 'unit': unit, 'scope': scope,
            'direction': 'lower' if unit in ('ms', 'ns', 's', 'MB', 'MiB', 'bytes') else 'unspecified',
            'implementationValue': value, 'comparatorValue': comparator,
            'ratio': ratio, 'samples': {'implementation': count, 'comparator': count if comparator is not None or ratio is not None else None,
                                      'unit': 'observations' if count is not None else None},
            'uncertainty': {'kind': 'reported confidence interval', 'level': .95,
                            'bounds': ci, 'unit': 'ratio'} if ci is not None else None,
            'outcome': outcome, 'policy': policy}
    return {key: value for key, value in record.items() if value is not None or key == 'unit'}


def phase_unit(name: str) -> str | None:
    if name in ('maxRssMiB', 'max_rss_mib'):
        return 'MiB'
    if name in ('frameworkImagesBeforeSetup', 'imagesBeforeSetup',
                'providerImagesLoaded', 'setupImagesLoaded'):
        return 'count'
    # process_metrics converts *Ns to milliseconds; preserve the original name.
    return 'ms'


def assign_machine(row: dict[str, Any], host: dict[str, Any], source: str,
                   adapter: dict[str, Any] | None = None) -> None:
    adapter = adapter or {}
    machine = row['machine']
    hostname = host.get('hostname') or host.get('machineId')
    machine.update({'id': hostname, 'label': hostname or host.get('label'),
                    'os': host.get('platform') or host.get('os'),
                    'osVersion': host.get('release') or host.get('kernel'),
                    'architecture': host.get('arch'),
                    'gpu': adapter.get('device') or adapter.get('description') or host.get('gpu'),
                    'driver': host.get('driver') or adapter.get('driver'),
                    'identitySource': source if hostname else None})
    if isinstance(machine['os'], dict):
        machine['os'] = None


def bind_phase(row: dict[str, Any], source: Source, provider: str,
               control: str) -> None:
    contract = source.data('contract') or {}
    row['workloadContractHash'] = (source.reference('contract') or {}).get('sha256')
    row['contractFrozenAt'] = reports.execution_times(contract)['contractFrozenAt']
    row['backend'] = 'metal' if 'metal' in source.registration['familyId'] else 'vulkan'
    row['implementation']['name'] = provider
    row['comparator']['name'] = control
    if provider == 'doe':
        row['implementation']['sourceCommit'] = contract.get('doeCommit')
    if control == 'dawn':
        row['comparator']['sourceCommit'] = contract.get('dawnCommitAsRecorded')
    binaries = contract.get('binaryManifest', {})
    if provider == 'doe':
        row['implementation']['binaryHash'] = binaries.get('libraries/libwebgpu_doe_c.dylib')
        compiler = binaries.get('libraries/libdoe_compiler.dylib')
        if compiler:
            row['implementation']['componentHashes']['compiler'] = compiler
    row['comparator']['binaryHash'] = binaries.get('libraries/libwebgpu_dawn.dylib') if control == 'dawn' else None
    host = contract.get('host', {})
    if isinstance(host, dict):
        assign_machine(row, host, 'contract.host')
    execution = source.data('execution')
    if isinstance(execution, str):
        stamp = reports.parse_utc_iso(execution.strip())
        if stamp:
            row['executedAt'] = reports.iso_utc(stamp)
            row['executionTimeSource'] = source.reference('execution')['path']
    elif isinstance(execution, dict):
        # Gate snapshots explicitly bracket execution; these are not contract clocks.
        stamp = execution.get('after', {}).get('utc')
        parsed = reports.parse_utc_iso(stamp)
        if parsed:
            row['executedAt'] = reports.iso_utc(parsed)
            row['executionTimeSource'] = source.reference('execution')['path'] + '#/after/utc'
    row['limitations'].append('No qualification follows from a phase summary or a valid host window.')


def native_catalog(source: Source) -> list[dict[str, Any]]:
    payload = source.payload
    contract = payload['contract']
    backend = payload['backend']
    policy_ref = source.policy['catalogClassification']
    policy_path = source.root / policy_ref['path']
    if file_hash(policy_path) != policy_ref['sha256']:
        raise ValueError('Catalog classification policy checksum mismatch')
    classification = catalog_classifier.load_policy(policy_path)
    if classification.get('id') != policy_ref['id']:
        raise ValueError('Catalog classification policy identity mismatch')
    rows = []
    for workload in payload['workloads']:
        row = source.row(workload['id'])
        if workload['kind'] == 'compilation':
            row['section'] = 'compiler'
            row['comparisonType'] = 'compiler-provider'
        row['backend'] = 'metal' if backend == 'apple-metal' else ('d3d12' if backend == 'windows-d3d12' else 'vulkan')
        row['machine']['os'] = 'darwin' if backend == 'apple-metal' else ('win32' if backend.startswith('windows-') else 'linux')
        row['workloadContractHash'] = payload.get('contractSha256')
        row['contractFrozenAt'] = reports.execution_times(contract)['contractFrozenAt']
        row['implementation'].update(name='doe', sourceCommit=contract.get('gitCommit'),
                                     binaryHash=contract.get('binaries', {}).get('doeRuntimeSha256'))
        row['comparator'].update(name='dawn', binaryHash=contract.get('binaries', {}).get('dawnRuntimeSha256'))
        row['implementation']['componentHashes'] = {k: v for k, v in contract.get('binaries', {}).items()
                                                   if k in ('doeEmitterSha256',) and v}
        row['executionStatus'] = 'succeeded' if workload['supported'] else 'failed'
        row['limitations'] += [
            'Only retained aggregate statistics; individual execution samples are unavailable.',
            'Duration-based 3/5-pair exceptions and command-repeat1 cold/warm aliasing limit interpretation; see REANALYSIS.md.',
            'The catalog policy classifies losses only; all other rows are not automatically wins.',
        ]
        if not workload['supported']:
            row['limitations'].append('Doe: ' + workload.get('doeStatus', 'unknown') + '; Dawn: ' + workload.get('dawnStatus', 'unknown'))
            row['verdict'] = 'failed-or-uncompared'
        else:
            for name, comparison in workload['comparisons'][classification['comparison']].items():
                doe = workload['arms']['doe'][name]
                dawn = workload['arms']['dawn'][name]
                outcome = 'loss' if catalog_classifier.is_loss(name, comparison, doe, classification) else 'not-classified-as-loss'
                row['metrics'].append(metric(name, 'MiB' if name == 'max_rss_mib' else 'ms',
                                             doe['p50'], dawn['p50'], comparison['p50'], count=workload['pairs'],
                                             ci=comparison['ci95'], outcome=outcome, policy=policy_ref))
            row['verdict'] = 'diagnostic'
        rows.append(row)
    return rows


def phase_cohort(source: Source) -> list[dict[str, Any]]:
    rows = []
    verdicts = source.data('verdicts') or {}
    for workload, section in source.payload['workloads'].items():
        for comparison, paired in section['paired'].items():
            provider, control = comparison.split('/')
            row = source.row(workload, comparison)
            bind_phase(row, source, provider, control)
            row['executionStatus'] = 'succeeded' if not source.payload.get('failedProcesses') else 'partially-failed'
            row['verdict'] = 'diagnostic'
            for name, ratio in paired['ratio'].items():
                observed = verdicts.get('results', {}).get(workload, {}).get(name, {})
                # Supplemental verdicts apply only to their declared provider pair.
                outcome = observed.get('classification', 'unknown') if comparison == 'doe/dawn' else 'unknown'
                row['metrics'].append(metric(name, phase_unit(name),
                    section['providers'][provider]['median'].get(name),
                    section['providers'][control]['median'].get(name), ratio['median'],
                    count=ratio.get('n'), ci=ratio.get('ci95'), outcome=outcome))
            row['limitations'].append('Aggregate row; raw process observations, when retained, are separate rows.')
            rows.append(row)
    raw_refs = [r for r in source.references if r['role'] == 'raw' or r['role'].startswith('raw:')]
    for raw_ref in raw_refs:
        if raw_ref['availability'] != 'retained':
            continue
        from bench.tools.dropin_phase_cohort import process_metrics
        path = source.root / raw_ref['path']
        stream = gzip.open(path, 'rt', encoding='utf-8') if path.suffix == '.gz' else path.open(encoding='utf-8')
        with stream:
            for i, line in enumerate(stream):
                payload = json.loads(line)
                provider = payload['provider']
                row = source.row(payload['workload'], raw_ref['path'] + ':process:' + str(i), 'execution')
                row['evidence'] = [r for r in row['evidence'] if not r['role'].startswith('raw:') or r == raw_ref]
                bind_phase(row, source, provider, 'unpaired')
                contract = source.data('contract') or {}
                binary = payload.get('binary', '').replace(chr(92), '/')
                matches = [(k, v) for k, v in contract.get('hashes', {}).items() if binary.endswith(k)]
                if len(matches) == 1:
                    row['implementation']['binaryHash'] = matches[0][1]
                result = payload.get('result', {})
                assign_machine(row, result.get('host', contract.get('host', {})), 'raw.result.host-or-contract.host',
                               {'device': result.get('adapterDevice')})
                row['executionStatus'] = 'succeeded' if payload.get('returnCode') == 0 and result.get('status') == 'ok' else 'failed'
                row['verdict'] = 'observed' if row['executionStatus'] == 'succeeded' else 'failed'
                row['correctness'] = {'status': 'passed' if result.get('oracleChecked') == result.get('iterations') and result.get('iterations', 0) > 0 else 'unknown', 'evidence': raw_ref['path'] + '#line=' + str(i + 1)}
                if row['executionStatus'] == 'succeeded':
                    row['metrics'] = [metric(k, phase_unit(k), v, scope='process', count=1)
                                      for k, v in process_metrics(payload).items()]
                else:
                    row['limitations'].append('Process failed: ' + str(payload.get('stderr', result)))
                # A whole-window timestamp is not a per-process execution timestamp.
                row['executedAt'] = reports.execution_times(result)['executedAt']
                row['executionTimeSource'] = raw_ref['path'] + '#line=' + str(i + 1) if row['executedAt'] else None
                rows.append(row)
    return rows


def browser_launches(source: Source) -> Iterator[tuple[str, dict[str, Any]]]:
    ref = source.reference('launches')
    if ref and ref['availability'] == 'retained':
        with tarfile.open(source.root / ref['path'], 'r:gz') as archive:
            for member in archive:
                if '/launches/' in member.name and member.name.endswith('.json') and '.spec.' not in member.name:
                    stream = archive.extractfile(member)
                    if stream is not None:
                        yield member.name, json.load(stream)


def browser_fresh_launch(source: Source) -> list[dict[str, Any]]:
    from bench.browser.fresh_launch.analyze import METRICS, launch_metrics
    units = {name: unit for name, _label, unit, _stat, _lower in METRICS}
    launches = list(browser_launches(source))
    rows = []
    for page, section in source.payload.get('pages', {}).items():
        for comparison, values in section['comparisons'].items():
            candidate, baseline = comparison.split('/')
            row = source.row(page, comparison)
            row['implementation']['name'] = candidate
            row['comparator']['name'] = baseline
            selected = [r for _, r in launches if r['spec']['page'] == page and r['spec']['arm'] in (candidate, baseline)]
            ends = [r.get('finishedAt') for r in selected if reports.parse_utc_iso(r.get('finishedAt'))]
            if ends:
                row['executedAt'] = reports.iso_utc(max(reports.parse_utc_iso(t) for t in ends))
                row['executionTimeSource'] = source.reference('launches')['path'] + '#max(finishedAt) for page/arms'
            if selected:
                assign_machine(row, selected[0].get('host', {}), 'launch.host', selected[0].get('adapterInfo'))
                row['backend'] = 'd3d12' if row['machine']['os'] == 'win32' else 'metal' if row['machine']['os'] == 'darwin' else None
            row['executionStatus'] = 'partially-failed' if any(r.get('ok') is False for r in selected) else 'succeeded' if selected else 'unknown'
            for name, value in values.items():
                arm = section['arms'].get(candidate, {}).get('metrics', {}).get(name, {})
                outcome = {'no clear difference': 'inconclusive'}.get(value['verdict'], value['verdict'])
                row['metrics'].append(metric(name, value.get('unit'), value.get('candidate'),
                    value.get('baseline'), value.get('ratio'), scope=value.get('label', name),
                    ci=value.get('ci95'), count=arm.get('n'), outcome=outcome))
            row['verdict'] = 'diagnostic'
            row['limitations'].append('Browser integration evidence cannot qualify native runtime or workspace behavior.')
            rows.append(row)
    for member, payload in launches:
        spec = payload['spec']
        row = source.row(spec['page'], member, 'execution')
        row.update(reports.execution_times(payload))
        row['executionTimeSource'] = source.reference('launches')['path'] + '#' + member + '/finishedAt' if row['executedAt'] else None
        assign_machine(row, payload.get('host', {}), 'launch.host', payload.get('adapterInfo'))
        row['backend'] = 'd3d12' if row['machine']['os'] == 'win32' else 'metal' if row['machine']['os'] == 'darwin' else None
        row['implementation']['name'] = spec['arm']
        row['executionStatus'] = 'succeeded' if payload.get('ok') else 'failed'
        row['verdict'] = 'observed' if payload.get('ok') else 'failed'
        oracle = payload.get('oracle', {})
        if oracle and all(oracle.get(k, {}).get('matched') for k in ('first', 'final')):
            row['correctness'] = {'status': 'passed', 'evidence': member + '#oracle'}
        else:
            row['limitations'].append('No full independent output oracle retained for this launch; visual checks do not substitute.')
        if spec.get('expectDoe') and payload.get('provider', {}).get('doeRuntimeMapped') is True:
            row['fallback'] = {'status': 'provider-observed', 'evidence': member + '#provider'}
        row['metrics'] = [metric(k, units.get(k), value, scope='single fresh browser launch', count=1)
                          for k, value in launch_metrics(payload).items()]
        rows.append(row)
    return rows


def workspace(source: Source) -> list[dict[str, Any]]:
    payload = source.payload
    rows = []
    for i, observed in enumerate(payload.get('rows', [])):
        scenario = observed.get('scenarioId') or observed.get('caseId') or observed.get('id') or 'scenario:' + str(i)
        row = source.row(scenario, 'scenario:' + str(i), 'execution')
        identity = payload.get('identity', {})
        assign_machine(row, identity.get('os', {}), 'identity.os')
        row['implementation'].update(name=observed.get('arm', observed.get('side', 'workspace')),
                                     sourceCommit=identity.get('git', {}).get('commit'))
        service_hash = identity.get('serviceSource', {}).get('sha256')
        if service_hash:
            row['implementation']['componentHashes']['serviceSource'] = service_hash
        for side, name in [('implementation', 'treatment'), ('comparator', 'control')]:
            browsers = payload.get('browsers', {}).get(name, [])
            if browsers:
                row[side]['binaryHash'] = browsers[0].get('sha256')
        row['comparator']['name'] = 'stock-automation'
        row['workloadContractHash'] = identity.get('comparisonPolicy', {}).get('sha256')
        row['executedAt'] = reports.execution_times(observed)['executedAt']
        row['executionTimeSource'] = source.registration['path'] + '#rows/' + str(i) if row['executedAt'] else None
        row['executionStatus'] = ('succeeded' if observed.get('taskSuccess') is True else 'failed'
                                  if observed.get('taskSuccess') is False or observed.get('error') else 'unknown')
        if isinstance(observed.get('taskSuccess'), bool):
            row['correctness'] = {'status': 'passed' if observed['taskSuccess'] else 'failed',
                                  'evidence': source.registration['path'] + '#rows/' + str(i) + '/taskSuccess'}
        for key, unit in [('latencyMs', 'ms'), ('observationBytes', 'bytes'),
                          ('incorrectActions', 'count'), ('taskSuccess', 'boolean'), ('recovered', 'boolean'),
                          ('constraintViolatingMutations', 'count'), ('mutationsDuringHumanControl', 'count'),
                          ('humanReviews', 'count'), ('duplicateEffects', 'count')]:
            if observed.get(key) is not None:
                row['metrics'].append(metric(key, unit, observed[key], scope='single behavioral scenario', count=1))
        row['verdict'] = row['executionStatus']
        row['limitations'].append('Workspace behavior and latency cannot establish native runtime or browser rendering qualification.')
        rows.append(row)
    summary = source.row(source.registration['familyId'], 'report-summary')
    summary['verdict'] = 'behavioral-status-only'
    advantage = payload.get('productAdvantage', {})
    if isinstance(advantage.get('established'), bool):
        summary['verdict'] = 'fixture-advantage-established' if advantage['established'] else 'fixture-advantage-not-established'
        summary['limitations'] += ['Does not establish ' + item for item in advantage.get('doesNotEstablish', [])]
    summary['limitations'].append('Behavioral summary does not supply execution dates or native-runtime qualification.')
    rows.append(summary)
    for i, requirement in enumerate(payload.get('requirements', [])):
        row = source.row(requirement.get('scenario', 'requirement:' + str(i)), 'requirement:' + str(i))
        row['verdict'] = 'requirement-' + requirement['status']
        row['acceptance'] = {'status': 'passed' if requirement['status'] == 'met' else 'failed',
                             'evidence': source.registration['path'] + '#requirements/' + str(i)}
        improvement = requirement.get('improvement', {})
        name = improvement.get('measure', 'improvement')
        unit = 'ms' if name.endswith('Ms') else 'bytes' if name.endswith('Bytes') else 'count'
        row['metrics'].append(metric(name, unit, improvement.get('treatment'), improvement.get('control'),
                                     improvement.get('ratio'), outcome=row['verdict'], scope='fixture requirement'))
        row['limitations'].append('Recorded fixture requirement, not release or native-runtime qualification.')
        rows.append(row)
    return rows


def compare_report(source: Source) -> list[dict[str, Any]]:
    """Read existing legacy compare receipts without reauthorizing their claims."""
    payload = source.payload
    participants = payload.get('participants', {})
    left, right = participants.get('left', {}), participants.get('right', {})
    rows = []
    for i, observed in enumerate(payload.get('workloads', [])):
        row = source.row(str(observed.get('id', i)))
        assign_machine(row, left.get('hostIdentity', {}), 'participants.left.hostIdentity', left.get('hostIdentity', {}).get('adapter'))
        row['backend'] = left.get('hostIdentity', {}).get('api')
        for side, participant in [('implementation', left), ('comparator', right)]:
            runtime = participant.get('runtimeIdentity', {})
            row[side].update(name=participant.get('product'), sourceCommit=runtime.get('sourceCommit'),
                             binaryHash=runtime.get('binarySha256'))
            delegate = runtime.get('nativeDelegate', {})
            if delegate.get('librarySha256'):
                row[side]['componentHashes']['provider'] = delegate['librarySha256']
        row['executionStatus'] = 'unknown'
        row['verdict'] = 'reported-' + str(payload.get('comparisonStatus', 'diagnostic'))
        for suffix in ('p50', 'p95', 'p99'):
            a = observed.get('baseline', {}).get('stats', {}).get(suffix + 'Ms')
            b = observed.get('comparison', {}).get('stats', {}).get(suffix + 'Ms')
            if a is not None or b is not None:
                row['metrics'].append(metric(suffix, 'ms', a, b, a / b if b and a is not None else None,
                                             scope='legacy reported timing; see original comparability'))
        row['limitations'].append('Historical claim/index labels are publication authority, not new qualification or current source evidence.')
        rows.append(row)
    return rows


def application(source: Source) -> list[dict[str, Any]]:
    """Expose retained application/compiler records without inventing admission."""
    payload = source.payload
    row = source.row(str(payload.get('module') or payload.get('reportId') or source.registration['familyId']))
    row['verdict'] = str(payload.get('disposition') or payload.get('outcome') or payload.get('classification') or 'unknown')
    if isinstance(payload.get('passed'), bool):
        row['acceptance'] = {'status': 'passed' if payload['passed'] else 'failed',
                             'evidence': source.registration['path'] + '#passed'}
    equivalence = payload.get('equivalence', {})
    if equivalence.get('oracleStatus') in ('pass', 'fail'):
        row['correctness'] = {'status': 'passed' if equivalence['oracleStatus'] == 'pass' else 'failed',
                              'evidence': source.registration['path'] + '#equivalence/oracleStatus'}
    row['limitations'] += payload.get('limitations', [])
    row['limitations'].append('No execution identities, dates or samples are inferred from an application verdict or report directory name.')
    for key in ('primaryRatio', 'lowerConfidenceRatio', 'aaMedianRatio'):
        if isinstance(payload.get(key), (int, float)):
            row['metrics'].append(metric(key, 'ratio', payload[key], scope='reported application decision'))
    for name, value in payload.get('regressionRatios', {}).items():
        row['metrics'].append(metric(name, 'ratio', ratio=value, scope='reported regression ratio'))
    performance = payload.get('performance', {})
    for phase, values in performance.get('comparison', {}).items():
        for stat, value in values.items():
            baseline = performance.get('baseline', {}).get(phase, {}).get(stat)
            row['metrics'].append(metric(phase + '.' + stat, performance.get('unit'), value, baseline,
                                        value / baseline if baseline else None, scope='reported application performance'))
    rows = [row]
    for provider, data in payload.get('providers', {}).items():
        if not isinstance(data, dict):
            continue
        for arm, metrics in data.get('arms', {}).items():
            observed = source.row(provider + '/' + arm, provider + '/' + arm)
            observed['implementation']['name'] = provider
            observed['verdict'] = row['verdict']
            def add_stats(prefix: str, values: dict[str, Any]) -> None:
                for name, value in values.items():
                    if isinstance(value, dict) and 'p50' in value:
                        unit = 'bytes' if name.endswith('Bytes') else 'ms' if name.endswith('Ms') else None
                        observed['metrics'].append(metric(prefix + name, unit, value['p50'],
                            count=len(value['samples']) if 'samples' in value else None, scope='retained ' + arm + ' aggregate'))
                    elif isinstance(value, dict):
                        add_stats(prefix + name + '.', value)
            add_stats('', metrics)
            rows.append(observed)
        if data.get('timingsMs'):
            observed = source.row(provider, provider)
            observed['implementation']['name'] = provider
            observed['verdict'] = row['verdict']
            for phase, timings in data['timingsMs'].items():
                for name, stats in timings.items():
                    for stat, value in stats.items():
                        observed['metrics'].append(metric(phase + '.' + name + '.' + stat, 'ms', value))
            for name, values in data.get('controls', {}).items():
                observed['metrics'].append(metric('control.' + name, 'ratio', ratio=values['ratio'],
                    ci=[values['lower'], values['upper']], count=len(values.get('pairedLogRatios', [])),
                    scope='same-provider control, not Doe versus Dawn'))
            rows.append(observed)
    for i, result in enumerate(payload.get('rows', [])):
        workload = str(result.get('shader') or result.get('stage') or result.get('variant') or 'process')
        observed = source.row(workload, 'record:' + str(i), 'execution' if 'exitCode' in result or 'cohort' in result else 'aggregate')
        observed['implementation']['name'] = result.get('arm')
        observed['backend'] = result.get('target')
        if isinstance(payload.get('contract'), dict):
            observed['workloadContractHash'] = reports.stable_hash(payload['contract'])
        observed['executedAt'] = reports.execution_times(result)['executedAt']
        observed['executionTimeSource'] = source.registration['path'] + '#rows/' + str(i) if observed['executedAt'] else None
        if 'exitCode' in result:
            observed['executionStatus'] = 'succeeded' if result['exitCode'] == 0 else 'failed'
            observed['verdict'] = observed['executionStatus']
        for name, value in result.items():
            if not isinstance(value, (int, float)) or isinstance(value, bool):
                continue
            unit = 'ms' if name.endswith('Ms') else 'ns' if name.endswith('Ns') else 'bytes' if name.endswith('Bytes') else None
            if unit and not name.startswith(('candidate', 'baseline')):
                observed['metrics'].append(metric(name, unit, value, scope='recorded process or iteration', count=1))
        for name in ('runWallNs', 'runCpuNs'):
            if isinstance(result.get(name), list):
                for j, value in enumerate(result[name]):
                    observed['metrics'].append(metric(name + '[' + str(j) + ']', 'ns', value,
                                                       scope='iteration within recorded application process', count=1))
        for stat in ('P50', 'P95'):
            candidate, baseline = result.get('candidate' + stat + 'Ns'), result.get('baseline' + stat + 'Ns')
            if candidate is not None:
                observed['metrics'].append(metric(stat, 'ns', candidate, baseline,
                    candidate / baseline if baseline else None, scope='retained compiler aggregate'))
        if isinstance(result.get('bytesOutEqual'), bool):
            observed['correctness'] = {'status': 'passed' if result['bytesOutEqual'] else 'failed',
                                      'evidence': source.registration['path'] + '#rows/' + str(i) + '/bytesOutEqual'}
        rows.append(observed)
    for i, result in enumerate(payload.get('compilations', [])):
        observed = source.row(result['stage'], 'compilation:' + str(i), 'execution')
        compiler = result['compiler']
        observed['implementation'].update(name=compiler, binaryHash=payload.get('inputs', {}).get(
            'doe_compiler' if compiler == 'doe' else compiler, {}).get('sha256'))
        observed['workloadContractHash'] = payload.get('fixtureManifestSha256')
        observed['executionStatus'] = 'succeeded' if result['returnCode'] == 0 else 'failed'
        observed['verdict'] = observed['executionStatus']
        observed['metrics'] = [metric('processWallMs', 'ms', result['processWallMs'], scope='compiler process', count=1)]
        rows.append(observed)
    for arm_index, arm in enumerate(payload.get('arms', [])):
        for sequence_index, result in enumerate(arm.get('sequences', [])):
            observed = source.row(result['sequence'], 'arm:' + str(arm_index) + '/sequence:' + str(sequence_index), 'execution')
            observed['implementation'].update(name=arm['runtime'], binaryHash=payload.get('inputs', {}).get(
                arm['runtime'] + '_library', {}).get('sha256'))
            observed['implementation']['componentHashes']['compiler'] = payload['inputs'][
                'doe_compiler' if arm['compiler'] == 'doe' else arm['compiler']]['sha256']
            observed['workloadContractHash'] = payload.get('fixtureManifestSha256')
            observed['executionStatus'] = 'succeeded' if result['returnCode'] == 0 else 'failed'
            observed['metrics'] = [metric('processWallMs', 'ms', result['processWallMs'], scope='projection process', count=1)]
            for j, sample in enumerate(result.get('observations', [])):
                if sample.get('kind') == 'sample':
                    observed['metrics'].append(metric('iteration:' + str(sample['iteration']), 'ms', sample['ms'],
                                                       scope='recorded iteration in projection process', count=1))
            rows.append(observed)
    for i, result in enumerate(payload.get('comparisons', [])):
        observed = source.row(result['name'], 'comparison:' + str(i))
        observed['verdict'] = str(payload.get('performanceClaim', 'diagnostic'))
        if 'medianRatio' in result:
            observed['metrics'] = [metric(result['name'], 'ratio', ratio=result['medianRatio'],
                count=len(result.get('ratios', [])), scope='reported compiler/runtime attribution, not qualification')]
        rows.append(observed)
    return rows


ADAPTERS = {'native-catalog': native_catalog, 'phase-cohort': phase_cohort,
            'browser-fresh-launch': browser_fresh_launch, 'workspace': workspace,
            'compare-report': compare_report, 'application': application}


def normalize_source(root: Path, registration: dict[str, Any],
                     policy: dict[str, Any]) -> list[dict[str, Any]]:
    expected = policy['adapters'].get(registration['adapter'])
    if expected != registration['adapterVersion'] or registration['adapter'] not in ADAPTERS:
        raise ValueError('Unsupported inventory adapter version: ' + registration['adapter'])
    if registration['section'] not in policy['adapterSections'][registration['adapter']]:
        raise ValueError('Adapter cannot change evidence surface: ' + registration['adapter'])
    source = make_source(root, registration, policy)
    if not source.payload:
        row = source.row(registration['familyId'], 'unavailable', 'unavailable')
        row['executionStatus'] = 'not-run' if registration['path'] is None else 'unknown'
        row['limitations'].append('Registered measurement evidence unavailable; no result is invented.')
        return [reports.finalize_inventory_row(row, policy['grouping'])]
    rows = ADAPTERS[registration['adapter']](source)
    if not rows:
        row = source.row(registration['familyId'])
        row['limitations'].append('Source format contains no recognized individual observations.')
        rows = [row]
    return [reports.finalize_inventory_row(row, policy['grouping']) for row in rows]
