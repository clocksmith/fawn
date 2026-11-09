// Personal workflow persistence on one host. Only verified runs publish; no graph import API.
// Exclusive file creation serializes processes. A crashed lock is never automatically stolen.
import { randomUUID } from 'node:crypto';
import { closeSync, constants, fsyncSync, fstatSync, lstatSync, mkdirSync, openSync,
  readFileSync, renameSync, unlinkSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { canonicalJson, sha256Hex } from '../journal.js';
import { TraceDag } from './trace-dag.js';
import { runWorkflow } from './workflow-agent.js';

const VERSION = 1;
const HASH = /^[a-f0-9]{64}$/;
const FILE_MODE = 0o600, DIRECTORY_MODE = 0o700;
const SOURCE_FILES = ['workflow-store.js', 'workflow-agent.js', 'trace-dag.js', 'controller.js',
  'agent.js', '../adapters/page-query.js', '../service.js'];
const exactKeys = (value, keys) => value && typeof value === 'object'
  && !Array.isArray(value) && Object.keys(value).sort().join() === [...keys].sort().join();
const identifier = value => typeof value === 'string' && value.trim().length > 0;
const fail = (code, message) => { throw Object.assign(Error(message), { code }); };
const locatorValid = value => Array.isArray(value) && value.length === 4
  && value.every(part => typeof part === 'string');
const hashMapValid = value => value && !Array.isArray(value) && typeof value === 'object'
  && Object.keys(value).length > 0 && Object.values(value).every(hash => typeof hash === 'string' && HASH.test(hash));
const RUN_KEYS = ['runId', 'startedAt', 'completedAt', 'mode', 'completed', 'reviewVerified',
  'verificationId', 'policyHash', 'sourceHashes', 'taskSourceHashes', 'planner', 'modelCalls',
  'cacheSteps', 'elapsedMs', 'fallbackReason', 'interventionRequests', 'workflowId', 'baseVersion', 'resultVersion'];

function privateStat(stat, directory = false) {
  if (!(directory ? stat.isDirectory() : stat.isFile())
    || stat.uid !== process.getuid() || (stat.mode & 0o077)) {
    fail('workflow-store-private', 'Workflow storage must be owner-only, owned by this process user');
  }
}

/** Trusted host API, not an HTTP body or a public recipe importer. The caller supplies an
 * independently implemented account verifier and task review oracle, never model callbacks.
 * Limits and lock timing are explicit host policy. The whole export, including key, is sensitive.
 */
export class WorkflowStore {
  constructor({ directory, ownerScope, limits, maxBytes, lockWaitMs, lockRetryMs }) {
    if (typeof process.getuid !== 'function') throw Error('Private workflow store requires POSIX permissions');
    if (!identifier(ownerScope) || !identifier(directory)
      || [maxBytes, lockWaitMs, lockRetryMs].some(value => !Number.isSafeInteger(value) || value < 1)) {
      throw Error('Workflow store requires owner scope, directory, byte limit and bounded lock timings');
    }
    new TraceDag(limits);
    this.directory = resolve(directory);
    mkdirSync(this.directory, { recursive: true, mode: DIRECTORY_MODE });
    privateStat(lstatSync(this.directory), true);
    this.path = join(this.directory, 'workflows.private.json');
    this.lockPath = join(this.directory, 'workflows.lock');
    this.ownerScope = ownerScope;
    this.limits = Object.freeze({ ...limits });
    this.maxBytes = maxBytes;
    this.lockWaitMs = lockWaitMs;
    this.lockRetryMs = lockRetryMs;
  }
  #namespace({ accountScope, template }) {
    if (!identifier(accountScope) || !identifier(template)) throw Error('Explicit account scope and template are required');
    return sha256Hex(canonicalJson([this.ownerScope, accountScope, template]));
  }
  #read() {
    let fd;
    try { fd = openSync(this.path, constants.O_RDONLY | constants.O_NOFOLLOW); }
    catch (error) {
      if (error.code === 'ENOENT') return { version: VERSION, workflows: {}, workflowVersions: {}, workflowRuns: [] };
      throw error;
    }
    let state;
    try {
      const stat = fstatSync(fd); privateStat(stat);
      if (stat.size > this.maxBytes) fail('workflow-store-limit', 'Workflow store exceeds the byte budget');
      state = JSON.parse(readFileSync(fd, 'utf8'));
    } finally { closeSync(fd); }
    return this.#validate(state);
  }
  #validate(state) {
    if (!exactKeys(state, ['version', 'workflows', 'workflowVersions', 'workflowRuns']) || state.version !== VERSION
      || [state.workflows, state.workflowVersions].some(value => !value || Array.isArray(value) || typeof value !== 'object')
      || !Array.isArray(state.workflowRuns)) throw Error('Invalid workflow store');
    for (const [id, pointer] of Object.entries(state.workflows)) {
      if (!HASH.test(id) || !exactKeys(pointer, ['revision', 'currentVersion'])
        || !Number.isSafeInteger(pointer.revision) || pointer.revision < 0
        || state.workflowVersions[pointer.currentVersion]?.workflowId !== id
        || state.workflowVersions[pointer.currentVersion]?.revision !== pointer.revision) throw Error('Invalid workflow pointer');
    }
    for (const entry of Object.values(state.workflowVersions)) {
      if (!exactKeys(entry, ['workflowId', 'revision', 'parentVersion', 'dag']) || !state.workflows[entry.workflowId]
        || !Number.isSafeInteger(entry.revision) || entry.revision < 0
        || !exactKeys(entry.dag, ['version', 'key', 'limits', 'nodes'])) throw Error('Invalid workflow namespace');
      TraceDag.restore(entry.dag, this.limits);
      if (entry.revision === 0 ? entry.parentVersion !== null
        : state.workflowVersions[entry.parentVersion]?.revision !== entry.revision - 1
          || state.workflowVersions[entry.parentVersion]?.workflowId !== entry.workflowId
          || state.workflowVersions[entry.parentVersion]?.dag.key !== entry.dag.key) throw Error('Invalid workflow lineage');
    }
    for (const run of state.workflowRuns) {
      if (!exactKeys(run, RUN_KEYS) || !identifier(run.runId) || !state.workflows[run.workflowId]
        || state.workflowVersions[run.baseVersion]?.workflowId !== run.workflowId
        || state.workflowVersions[run.resultVersion]?.workflowId !== run.workflowId
        || !['learn', 'reuse', 'fresh'].includes(run.mode)
        || typeof run.completed !== 'boolean' || typeof run.reviewVerified !== 'boolean'
        || !identifier(run.verificationId) || !HASH.test(run.policyHash)
        || !hashMapValid(run.sourceHashes) || !hashMapValid(run.taskSourceHashes)
        || !exactKeys(run.planner, ['declaredModel', 'observedModels'])
        || !(run.planner.declaredModel === null || typeof run.planner.declaredModel === 'string')
        || !Array.isArray(run.planner.observedModels) || !run.planner.observedModels.every(identifier)
        || ![run.startedAt, run.completedAt].every(value => typeof value === 'string' && Number.isFinite(Date.parse(value)))
        || ![run.modelCalls, run.cacheSteps, run.interventionRequests].every(value => Number.isSafeInteger(value) && value >= 0)
        || !Number.isFinite(run.elapsedMs) || run.elapsedMs < 0
        || !(run.fallbackReason === null || typeof run.fallbackReason === 'string')) throw Error('Invalid workflow run');
    }
    return state;
  }
  #write(state) {
    this.#validate(state);
    const bytes = JSON.stringify(state) + '\n';
    if (Buffer.byteLength(bytes) > this.maxBytes) fail('workflow-store-limit', 'Workflow store exceeds the byte budget');
    const temporary = `${this.path}.${randomUUID()}.tmp`;
    const fd = openSync(temporary, 'wx', FILE_MODE);
    try { writeFileSync(fd, bytes); fsyncSync(fd); }
    catch (error) { unlinkSync(temporary); throw error; }
    finally { closeSync(fd); }
    try {
      renameSync(temporary, this.path);
      const directoryFd = openSync(this.directory, 'r');
      try { fsyncSync(directoryFd); } finally { closeSync(directoryFd); }
    } finally {
      try { unlinkSync(temporary); } catch (error) { if (error.code !== 'ENOENT') throw error; }
    }
  }
  async #transaction(update) {
    const deadline = performance.now() + this.lockWaitMs;
    let fd;
    for (;;) {
      try { fd = openSync(this.lockPath, 'wx', FILE_MODE); break; }
      catch (error) {
        if (error.code !== 'EEXIST') throw error;
        if (performance.now() >= deadline) fail('workflow-store-busy', 'Workflow publisher is locked; no browser action was retried');
        await delay(Math.min(this.lockRetryMs, Math.max(1, deadline - performance.now())));
      }
    }
    try {
      writeFileSync(fd, JSON.stringify({ pid: process.pid }));
      fsyncSync(fd);
      // Reload only after acquiring the cross-process lock; a checkout is never write authority.
      return update(this.#read());
    } finally { closeSync(fd); unlinkSync(this.lockPath); }
  }
  async checkout(scope) {
    const id = this.#namespace(scope);
    const initial = this.#read();
    const snapshot = state => {
      const pointer = state.workflows[id];
      return structuredClone({ revision: pointer.revision, versionId: pointer.currentVersion,
        dag: state.workflowVersions[pointer.currentVersion].dag });
    };
    if (initial.workflows[id]) return snapshot(initial);
    return this.#transaction(state => {
      if (!state.workflows[id]) {
        const versionId = randomUUID();
        state.workflowVersions[versionId] = { workflowId: id, revision: 0, parentVersion: null,
          dag: new TraceDag(this.limits).export() };
        state.workflows[id] = { revision: 0, currentVersion: versionId };
        this.#write(state);
      }
      return snapshot(state);
    });
  }
  async #complete(scope, checkout, trace, receipt) {
    return this.#transaction(state => {
      const id = this.#namespace(scope), pointer = state.workflows[id];
      const latest = state.workflowVersions[pointer?.currentVersion];
      if (!latest || latest.dag.key !== checkout.dag.key || latest.revision < checkout.revision) {
        fail('workflow-store-lineage', 'Workflow lineage changed; refusing publication');
      }
      const dag = TraceDag.restore(latest.dag, this.limits);
      if (trace) dag.publish(trace.records, trace.terminal);
      const next = dag.export();
      if (canonicalJson(next) !== canonicalJson(latest.dag)) {
        if (!Number.isSafeInteger(latest.revision + 1)) throw Error('Workflow revision exhausted');
        const versionId = randomUUID();
        state.workflowVersions[versionId] = { workflowId: id, revision: latest.revision + 1,
          parentVersion: pointer.currentVersion, dag: next };
        state.workflows[id] = { revision: latest.revision + 1, currentVersion: versionId };
      }
      const current = state.workflows[id];
      state.workflowRuns.push({ ...receipt, workflowId: id, baseVersion: checkout.versionId,
        resultVersion: current.currentVersion });
      this.#write(state);
      return { status: trace ? 'published' : 'unchanged', revision: current.revision,
        versionId: current.currentVersion, receiptStored: true };
    });
  }
  async run({ accountScope, verifyAccount, verificationId, taskSourceHashes, disclosurePolicy, ...options }) {
    if (typeof verifyAccount !== 'function' || typeof options.verifyReview !== 'function'
      || !identifier(verificationId) || !hashMapValid(taskSourceHashes)) {
      throw Error('Trusted account and review verifiers are required');
    }
    const frozenTaskSources = structuredClone(taskSourceHashes);
    // Snapshot trusted disclosure rules before any asynchronous work. Models cannot add rules.
    const policy = structuredClone(disclosurePolicy);
    if (!exactKeys(policy, ['origin', 'fields', 'intermediateClicks'])
      || !['http:', 'https:'].includes(new URL(policy.origin).protocol)
      || new URL(policy.origin).origin !== policy.origin || !Array.isArray(policy.fields)
      || !Array.isArray(policy.intermediateClicks) || !policy.intermediateClicks.every(locatorValid)
      || !policy.fields.every(field => exactKeys(field, ['kind', 'slot', 'locator'])
        && ['fill', 'select'].includes(field.kind) && identifier(field.slot) && locatorValid(field.locator))) {
      throw Error('Explicit origin, slot-to-field rules and intermediate click policy are required');
    }
    const scope = { accountScope, template: options.template };
    const identity = { service: options.service, owner: options.owner, pageId: options.pageId };
    const assertContext = async () => {
      options.signal.throwIfAborted();
      if (await verifyAccount(identity) !== accountScope) fail('workflow-account-mismatch', 'Website account does not match this private workflow scope');
    };
    await assertContext();
    const checkout = await this.checkout(scope);
    const sourceHashes = Object.fromEntries(SOURCE_FILES.map(path => [path,
      sha256Hex(readFileSync(new URL(path, import.meta.url)))]));
    const observedModels = new Set();
    const planner = { identity: options.planner?.identity, next: async (...args) => {
      const proposal = await options.planner.next(...args);
      for (const model of proposal.usage?.models ?? []) observedModels.add(model);
      return proposal;
    } };
    const startedAt = new Date().toISOString();
    let learned = null;
    const result = await runWorkflow({ ...options, accountScope, planner, dag: TraceDag.restore(checkout.dag, this.limits),
      intermediateClicks: policy.intermediateClicks, assertContext,
      authorizeStep: async step => {
        const permitted = step.origin === policy.origin && (step.kind === 'click'
          ? policy.intermediateClicks.some(locator => canonicalJson(locator) === canonicalJson(step.locator))
          : policy.fields.some(field => field.kind === step.kind && field.slot === step.slot
            && canonicalJson(field.locator) === canonicalJson(step.locator)));
        if (!permitted) fail('workflow-disclosure-denied', 'Input slot is not authorized for this field and origin');
      },
      publishTrace: async trace => { learned = trace; },
    });
    let persistence;
    try {
      if (result.completed && learned) await assertContext();
      persistence = await this.#complete(scope, checkout, result.completed ? learned : null, {
        runId: randomUUID(), startedAt, completedAt: new Date().toISOString(), mode: options.mode,
        completed: result.completed, reviewVerified: result.verification?.completed === true,
        verificationId, policyHash: sha256Hex(canonicalJson(policy)), sourceHashes, taskSourceHashes: frozenTaskSources,
        planner: { declaredModel: options.planner?.identity?.model ?? null, observedModels: [...observedModels] },
        modelCalls: result.modelCalls, cacheSteps: result.cacheSteps, elapsedMs: result.elapsedMs,
        fallbackReason: result.fallbackReason, interventionRequests: result.interventionRequests,
      });
    } catch (error) {
      // A verified browser result remains true even if saving fails. Never rerun its effects.
      persistence = { status: 'failed', receiptStored: false,
        code: error.code ?? 'workflow-publication-failed', detail: error.message };
    }
    return { ...result, persistence };
  }
}
