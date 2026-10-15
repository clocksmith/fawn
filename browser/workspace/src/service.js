// Fawn workspace — shared observation, action and recovery service
//
// The only holder of the workspace browser. Owners observe and submit intent; the trusted
// dispatcher re-checks authority at dispatch, and the journal keeps outcomes across restart.

import { join } from 'node:path';
import {
  REGIONS_SCOPE,
  authenticateAdmin,
  authenticateOwner,
  checkAdmission,
  checkDispatchAuthority,
  validateRegionSelectors,
} from './admission.js';
import { issueCredential } from './authority.js';
import { evaluateConditions, validateConditions } from './conditions.js';
import { REJECTION_CODES, isWorkspaceError, workspaceError } from './errors.js';
import { Journal } from './journal.js';
import {
  ACTION_STATUS,
  OUTCOMES,
  RECORD_TYPES,
  applyRecord,
  describeAction,
  replay,
} from './state.js';

const NONATOMIC_BOUNDARY = Object.freeze({
  isAtomic: false,
  statement: 'Dispatch-time checks and the browser effect are separate steps; page, human or '
    + 'external changes can intervene, and effects already underway cannot be undone.',
});

const KIND_COMPLETION_BASIS = Object.freeze({
  navigate: 'navigation-committed',
  click: 'input-delivered',
  fill: 'value-verified',
  select: 'value-verified',
  download: 'artifact-saved',
});

const RESTART_RULES = Object.freeze([
  {
    matches: (action) => action.status === ACTION_STATUS.QUEUED,
    type: RECORD_TYPES.ACTION_NOT_DISPATCHED,
    body: { stage: 'restart', code: 'restart-invalidated-queue', detail: 'queued before restart' },
  },
  {
    matches: (action) => action.status === ACTION_STATUS.DISPATCHED && !action.outcome,
    type: RECORD_TYPES.ACTION_OUTCOME,
    body: {
      outcome: OUTCOMES.UNKNOWN,
      basis: 'restart-after-dispatch-start',
      evidence: { detail: 'dispatch began before restart and no outcome was recorded' },
      boundary: NONATOMIC_BOUNDARY,
    },
  },
]);

const NO_HOOKS = Object.freeze({});
const FULL_VIEW = 'full';
const OUTLINE_VIEW = 'outline';

/** Validate an observe command's read options against config. */
function validateObservationRead(command, config) {
  const read = {
    regions: command.regions ?? [],
    regionGroups: command.regionGroups ?? [],
    view: command.view ?? FULL_VIEW,
    sinceVersion: command.sinceVersion,
  };
  if (command.regions !== undefined) validateRegionSelectors(read.regions, config, 'regions');
  if (command.regionGroups !== undefined) validateRegionSelectors(read.regionGroups, config, 'regionGroups');
  if (!config.observationViews.includes(read.view)) {
    throw workspaceError(REJECTION_CODES.INVALID_COMMAND, 'view', { path: 'view', expected: config.observationViews, received: read.view });
  }
  if (read.sinceVersion !== undefined && !Number.isInteger(read.sinceVersion)) {
    throw workspaceError(REJECTION_CODES.INVALID_COMMAND, 'sinceVersion: expected integer', { path: 'sinceVersion' });
  }
  return read;
}

/**
 * Compact view for agents: each requested region's outline, or `unchanged` when its digest
 * matches the base version from the same owner generation. A missing base yields full outlines.
 * Digests, handle tables and element lists stay in the journal for admission.
 */
function outlineView(workspace, recorded, snapshot, sinceVersion) {
  const base = sinceVersion === undefined ? null : workspace.observations.get(sinceVersion) ?? null;
  const regions = Object.fromEntries(Object.entries(snapshot.regionDigests).map(([selector, digest]) => {
    if (digest === null) return [selector, { missing: true }];
    if (base?.regions?.[selector] === digest) return [selector, { unchanged: true }];
    return [selector, { outline: snapshot.regionOutlines[selector] ?? '' }];
  }));
  return {
    observationVersion: recorded.observationVersion,
    url: recorded.url,
    baseVersion: base ? sinceVersion : null,
    regions,
    regionGroups: snapshot.regionGroups ?? {},
    boundary: { isAtomic: false },
  };
}
const PROFILE_DIRECTORY = 'profiles';
const ARTIFACT_DIRECTORY = 'artifacts';

class Mutex {
  constructor() {
    this._tail = Promise.resolve();
  }

  run(task) {
    const result = this._tail.then(task);
    this._tail = result.then(() => {}, () => {});
    return result;
  }
}

function deferred() {
  let settle;
  const promise = new Promise((resolve) => {
    settle = resolve;
  });
  return { promise, settle };
}

function withTimeout(promise, timeoutMs) {
  let timer;
  const timeout = new Promise((_, rejectTimeout) => {
    timer = setTimeout(() => rejectTimeout(Object.assign(new Error('effect timed out'), { isTimeout: true })),
      timeoutMs);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

function stripCredentials(command) {
  const { ownerCredential, adminCredential, ...request } = command;
  return request;
}

class WorkspaceService {
  #journal;
  #state;
  #config;
  #adapter;
  #hooks;
  #clock;
  #runtime = new Map();
  #ready = false;

  constructor({ journal, state, config, adapter, hooks, clock }) {
    this.#journal = journal;
    this.#state = state;
    this.#config = config;
    this.#adapter = adapter;
    this.#hooks = hooks;
    this.#clock = clock;
  }

  /**
   * Restore durable authority, settle interrupted actions, then accept commands.
   * @param {{stateDir: string, config: object, createAdapter: Function, hooks?: object,
   *   clock?: () => number}} options
   */
  static async open({ stateDir, config, createAdapter, hooks = NO_HOOKS, clock = Date.now }) {
    const journal = Journal.open({
      directory: stateDir,
      fileName: config.journal.fileName,
      lockFileName: config.journal.lockFileName,
    });
    try {
      const state = replay(journal.records);
      const adapter = createAdapter({
        config,
        profileRoot: join(stateDir, PROFILE_DIRECTORY),
        artifactRoot: join(stateDir, ARTIFACT_DIRECTORY),
      });
      const service = new WorkspaceService({ journal, state, config, adapter, hooks, clock });
      service.#restore();
      return service;
    } catch (error) {
      journal.close();
      throw error;
    }
  }

  #append(type, body) {
    const record = this.#journal.append(type, body);
    applyRecord(this.#state, record);
    return record;
  }

  #restore() {
    const settled = [];
    for (const workspace of this.#state.workspaces.values()) {
      for (const action of workspace.actions.values()) {
        const rule = RESTART_RULES.find((candidate) => candidate.matches(action));
        if (!rule) continue;
        this.#append(rule.type, { workspaceId: workspace.workspaceId, actionId: action.request.actionId, ...rule.body });
        settled.push(action.request.actionId);
      }
    }
    this.#append(RECORD_TYPES.SERVICE_RESTORED, {
      recordCount: this.#journal.records.length,
      recovery: this.#journal.recovery,
      settledActionIds: settled,
      protocolVersion: this.#config.protocolVersion,
      configVersion: this.#config.configVersion,
    });
    this.#ready = true;
  }

  #workspace(workspaceId) {
    if (!this.#ready) throw workspaceError(REJECTION_CODES.AUTHORITY_UNAVAILABLE, 'service is not restored');
    const workspace = this.#state.workspaces.get(workspaceId);
    if (!workspace) throw workspaceError(REJECTION_CODES.UNKNOWN_WORKSPACE, String(workspaceId), { workspaceId });
    return workspace;
  }

  #runtimeFor(workspaceId) {
    if (!this.#runtime.has(workspaceId)) {
      this.#runtime.set(workspaceId, { mutex: new Mutex(), queue: [], pumping: false, opened: null });
    }
    return this.#runtime.get(workspaceId);
  }

  async #page(workspaceId) {
    const runtime = this.#runtimeFor(workspaceId);
    runtime.opened ??= this.#bounded(this.#adapter.openWorkspace(workspaceId)).catch((error) => {
      runtime.opened = null;
      throw error;
    });
    await runtime.opened;
  }

  /** Browser reads are bounded so a stuck page cannot hold the dispatch/handoff lock. */
  #bounded(promise) {
    return withTimeout(promise, this.#config.dispatch.actionTimeoutMs);
  }

  #snapshot(workspaceId, read = {}) {
    return this.#bounded(this.#adapter.snapshot(workspaceId, read));
  }

  #nowMs() {
    return this.#clock();
  }

  #probe(workspaceId) {
    return {
      url: () => this.#bounded(this.#adapter.probeUrl(workspaceId)),
      element: (selector) => this.#bounded(this.#adapter.probeElement(workspaceId, selector)),
    };
  }

  /** Record a new observation version from a live snapshot. */
  #recordObservation(workspace, snapshot, reason) {
    const issuedMs = this.#nowMs();
    const targetDigests = Object.fromEntries(snapshot.elements.map((element) => [element.selector, element.digest]));
    const targetFormActions = Object.fromEntries(snapshot.elements
      .filter((element) => element.formAction)
      .map((element) => [element.selector, element.formAction]));
    const targetNavigations = Object.fromEntries(snapshot.elements
      .filter((element) => element.href)
      .map((element) => [element.selector, element.href]));
    const handles = Object.fromEntries(snapshot.elements
      .filter((element) => element.handle)
      .map((element) => [element.handle, element.selector]));
    this.#append(RECORD_TYPES.OBSERVATION_RECORDED, {
      workspaceId: workspace.workspaceId,
      observationVersion: workspace.observationVersion + 1,
      ownerGeneration: workspace.ownerGeneration,
      reason,
      url: snapshot.url,
      contentHash: snapshot.contentHash,
      issuedAt: new Date(issuedMs).toISOString(),
      expiresAt: new Date(issuedMs + this.#config.observation.maxAgeMs).toISOString(),
      targetDigests,
      targetFormActions,
      targetNavigations,
      handles,
      regions: snapshot.regionDigests,
    });
    return { ...workspace.observation, title: snapshot.title, text: snapshot.text, elements: snapshot.elements };
  }

  /**
   * Create a durable workspace; the admin credential is returned once.
   */
  async createWorkspace({ workspaceId }) {
    if (!this.#ready) throw workspaceError(REJECTION_CODES.AUTHORITY_UNAVAILABLE, 'service is not restored');
    if (typeof workspaceId !== 'string' || !/^[a-z0-9-]+$/.test(workspaceId)) {
      throw workspaceError(REJECTION_CODES.INVALID_COMMAND, 'workspaceId: expected [a-z0-9-]+', {
        path: 'workspaceId',
        received: workspaceId,
      });
    }
    if (this.#state.workspaces.has(workspaceId)) {
      throw workspaceError(REJECTION_CODES.DUPLICATE_WORKSPACE, workspaceId, { workspaceId });
    }
    const { credential, credentialHash } = issueCredential(this.#config.credentials.secretBytes);
    this.#append(RECORD_TYPES.WORKSPACE_CREATED, { workspaceId, adminCredentialHash: credentialHash });
    return { workspaceId, adminCredential: credential };
  }

  async grantAuthorization({ workspaceId, adminCredential, grantId, ownerId, intents, actionKinds,
    targetOrigins, expiresAt = null }) {
    const workspace = this.#workspace(workspaceId);
    authenticateAdmin(workspace, adminCredential);
    for (const [path, list] of [['intents', intents], ['actionKinds', actionKinds], ['targetOrigins', targetOrigins]]) {
      if (!Array.isArray(list) || list.length === 0 || list.some((item) => typeof item !== 'string')) {
        throw workspaceError(REJECTION_CODES.INVALID_COMMAND, `${path}: expected non-empty string array`, { path });
      }
    }
    const unsupported = actionKinds.filter((kind) => !this.#config.actionKinds.includes(kind));
    if (unsupported.length > 0) {
      throw workspaceError(REJECTION_CODES.UNSUPPORTED_ACTION_KIND, unsupported.join(', '), { received: unsupported });
    }
    if (typeof grantId !== 'string' || typeof ownerId !== 'string' || !grantId || !ownerId) {
      throw workspaceError(REJECTION_CODES.INVALID_COMMAND, 'grantId and ownerId: expected non-empty strings', {
        received: { grantId, ownerId },
      });
    }
    if (expiresAt !== null && Number.isNaN(Date.parse(expiresAt))) {
      throw workspaceError(REJECTION_CODES.INVALID_COMMAND, 'expiresAt: expected ISO time or null', { path: 'expiresAt' });
    }
    const origins = targetOrigins.map((origin, index) => {
      try {
        return new URL(origin).origin;
      } catch {
        throw workspaceError(REJECTION_CODES.INVALID_COMMAND, `targetOrigins[${index}]: expected URL origin`, {
          path: `targetOrigins[${index}]`,
          received: origin,
        });
      }
    });
    return this.#runtimeFor(workspaceId).mutex.run(() => {
      this.#append(RECORD_TYPES.AUTHORIZATION_GRANTED, {
        workspaceId,
        grantId,
        grant: { ownerId, intents, actionKinds, targetOrigins: origins, expiresAt },
      });
      return { grantId };
    });
  }

  async revokeAuthorization({ workspaceId, adminCredential, grantId }) {
    const workspace = this.#workspace(workspaceId);
    authenticateAdmin(workspace, adminCredential);
    return this.#runtimeFor(workspaceId).mutex.run(() => {
      this.#append(RECORD_TYPES.AUTHORIZATION_REVOKED, { workspaceId, grantId });
      return { grantId };
    });
  }

  /**
   * Hand the workspace to a new owner. The generation advances durably before the new owner can
   * act; queued commands of earlier generations are ended undispatched. Effects already underway
   * are not undone and are returned among the outstanding records.
   */
  async assignOwner({ workspaceId, adminCredential, ownerId }) {
    const workspace = this.#workspace(workspaceId);
    authenticateAdmin(workspace, adminCredential);
    if (typeof ownerId !== 'string' || ownerId.length === 0) {
      throw workspaceError(REJECTION_CODES.INVALID_COMMAND, 'ownerId: expected non-empty string', { path: 'ownerId' });
    }
    const runtime = this.#runtimeFor(workspaceId);
    return runtime.mutex.run(() => {
      const previous = { ownerId: workspace.ownerId, ownerGeneration: workspace.ownerGeneration };
      const { credential, credentialHash } = issueCredential(this.#config.credentials.secretBytes);
      this.#append(RECORD_TYPES.OWNERSHIP_ADVANCED, {
        workspaceId,
        ownerId,
        ownerGeneration: previous.ownerGeneration + 1,
        ownerCredentialHash: credentialHash,
        previous,
      });
      for (const action of workspace.actions.values()) {
        if (action.status !== ACTION_STATUS.QUEUED) continue;
        this.#append(RECORD_TYPES.ACTION_NOT_DISPATCHED, {
          workspaceId,
          actionId: action.request.actionId,
          stage: 'handoff',
          code: REJECTION_CODES.STALE_OWNER_GENERATION,
          detail: `owner generation advanced to ${workspace.ownerGeneration}`,
        });
      }
      for (const entry of runtime.queue.splice(0)) entry.settle(this.#describe(workspace, entry.actionId));
      return {
        workspaceId,
        ownerId,
        ownerGeneration: workspace.ownerGeneration,
        ownerCredential: credential,
        outstanding: this.#outstanding(workspace),
        boundary: NONATOMIC_BOUNDARY,
      };
    });
  }

  /**
   * Observe the workspace page under the current owner generation; returns a new version.
   * `regions` names page regions whose digests the observation records, so a later action can
   * bind its staleness to them instead of to the whole page.
   */
  async observe(command) {
    const workspace = this.#workspace(command.workspaceId);
    authenticateOwner(workspace, command);
    const read = validateObservationRead(command, this.#config);
    await this.#page(command.workspaceId);
    return this.#runtimeFor(command.workspaceId).mutex.run(async () => {
      authenticateOwner(workspace, command);
      const snapshot = await this.#snapshot(command.workspaceId, {
        regions: read.regions,
        regionGroups: read.regionGroups,
        outline: read.view === OUTLINE_VIEW,
      });
      const recorded = this.#recordObservation(workspace, snapshot, 'observe');
      if (read.view === OUTLINE_VIEW) return outlineView(workspace, recorded, snapshot, read.sinceVersion);
      return { ...recorded, boundary: NONATOMIC_BOUNDARY };
    });
  }

  /**
   * Admit an action. Intent is durable before this resolves; `result` resolves with the action
   * record once it is dispatched, rejected before dispatch, or ended by handoff.
   */
  async submitAction(command) {
    const workspace = this.#workspace(command.workspaceId);
    const runtime = this.#runtimeFor(command.workspaceId);
    const admitted = await runtime.mutex.run(() => {
      let grantId;
      let resolved;
      try {
        ({ grantId, command: resolved } = checkAdmission(workspace, command, this.#config, this.#nowMs()));
      } catch (error) {
        if (isWorkspaceError(error)) {
          this.#append(RECORD_TYPES.COMMAND_REJECTED, {
            workspaceId: command.workspaceId,
            actionId: typeof command.actionId === 'string' ? command.actionId : null,
            stage: 'admission',
            code: error.code,
          });
        }
        throw error;
      }
      const request = { ...stripCredentials(resolved), grantId, effect: resolved.effect ?? { external: false } };
      this.#append(RECORD_TYPES.ACTION_ADMITTED, request);
      this.#hooks.afterIntentPersisted?.(request);
      const settlement = deferred();
      runtime.queue.push({ actionId: request.actionId, settle: settlement.settle });
      return { actionId: request.actionId, result: settlement.promise };
    });
    this.#pump(command.workspaceId);
    return admitted;
  }

  async #pump(workspaceId) {
    const runtime = this.#runtimeFor(workspaceId);
    if (runtime.pumping) return;
    runtime.pumping = true;
    try {
      while (runtime.queue.length > 0) {
        const entry = runtime.queue.shift();
        try {
          entry.settle(await this.#dispatchOne(workspaceId, entry.actionId));
        } catch (error) {
          // The journal or service closed mid-dispatch; the durable record, not this reply, is authoritative.
          entry.settle({ actionId: entry.actionId, serviceError: { code: error.code ?? null, message: error.message } });
        }
      }
    } finally {
      runtime.pumping = false;
    }
  }

  async #checkLiveDispatch(workspace, action) {
    const { workspaceId } = workspace;
    const request = action.request;
    checkDispatchAuthority(workspace, request, this.#nowMs());
    const scope = request.observationScope;
    const regions = scope?.kind === REGIONS_SCOPE ? scope.selectors : [];
    const snapshot = await this.#snapshot(workspaceId, { regions });
    if (snapshot.contentHash !== workspace.observation.contentHash || snapshot.url !== workspace.observation.url) {
      this.#recordObservation(workspace, snapshot, 'page-change');
    }
    const live = { url: snapshot.url, regions: snapshot.regionDigests };
    checkDispatchAuthority(workspace, request, this.#nowMs(), live);
    const probe = this.#probe(workspaceId);
    let targetDigest = null;
    if (request.target.selector) {
      const element = await probe.element(request.target.selector);
      if (element.count !== 1 || element.digest !== request.target.digest) {
        throw workspaceError(REJECTION_CODES.TARGET_CHANGED, `target ${request.target.selector}`, {
          count: element.count,
        });
      }
      targetDigest = element.digest;
    }
    const preconditions = await evaluateConditions(request.preconditions ?? [], probe);
    if (!preconditions.allEstablished) {
      throw workspaceError(REJECTION_CODES.PRECONDITION_UNESTABLISHED, 'dispatch preconditions', {
        results: preconditions.results,
      });
    }
    return {
      targetDigest,
      preconditionResults: preconditions.results,
      observationScope: scope ?? { kind: 'page' },
      liveRegions: regions.length > 0 ? live.regions : null,
    };
  }

  async #dispatchOne(workspaceId, actionId) {
    const workspace = this.#workspace(workspaceId);
    const runtime = this.#runtimeFor(workspaceId);
    const begun = await runtime.mutex.run(async () => {
      const action = workspace.actions.get(actionId);
      if (action.status !== ACTION_STATUS.QUEUED) return { settled: this.#describe(workspace, actionId) };
      let checks;
      try {
        await this.#page(workspaceId);
        checks = await this.#checkLiveDispatch(workspace, action);
      } catch (error) {
        this.#append(RECORD_TYPES.ACTION_NOT_DISPATCHED, {
          workspaceId,
          actionId,
          stage: 'dispatch',
          code: isWorkspaceError(error) ? error.code : REJECTION_CODES.PRECONDITION_UNESTABLISHED,
          detail: error.message,
        });
        return { settled: this.#describe(workspace, actionId) };
      }
      this.#append(RECORD_TYPES.ACTION_DISPATCH_STARTED, {
        workspaceId,
        actionId,
        ownerGeneration: workspace.ownerGeneration,
        observationVersion: workspace.observationVersion,
        ...checks,
      });
      this.#hooks.afterDispatchStarted?.(action.request);
      return { action };
    });
    if (begun.settled) return begun.settled;
    const outcome = await this.#performEffect(workspaceId, begun.action.request);
    return runtime.mutex.run(async () => {
      let postObservationVersion = null;
      try {
        const snapshot = await this.#snapshot(workspaceId);
        if (snapshot.contentHash !== workspace.observation.contentHash || snapshot.url !== workspace.observation.url) {
          this.#recordObservation(workspace, snapshot, 'post-action');
        }
        postObservationVersion = workspace.observationVersion;
      } catch (error) {
        outcome.evidence = { ...outcome.evidence, postObservationError: error.message };
      }
      this.#append(RECORD_TYPES.ACTION_OUTCOME, {
        workspaceId,
        actionId,
        ...outcome,
        postObservationVersion,
        boundary: NONATOMIC_BOUNDARY,
      });
      this.#hooks.afterOutcome?.(begun.action.request);
      return this.#describe(workspace, actionId);
    });
  }

  async #performEffect(workspaceId, request) {
    let effect;
    try {
      effect = await withTimeout(this.#adapter.execute(workspaceId, request), this.#config.dispatch.actionTimeoutMs);
    } catch (error) {
      return {
        outcome: OUTCOMES.UNKNOWN,
        basis: error.isTimeout ? 'effect-timeout' : 'effect-exception',
        evidence: { message: error.message },
      };
    }
    this.#hooks.afterEffect?.(request);
    const completion = request.completion;
    if (completion?.completedWhen?.length) return this.#awaitCompletion(workspaceId, completion, effect);
    if (request.kind === 'fill' || request.kind === 'select') {
      const check = await evaluateConditions(
        [{ kind: 'input-value-equals', selector: request.target.selector, value: request.params.value }],
        this.#probe(workspaceId),
      );
      return check.allEstablished
        ? { outcome: OUTCOMES.COMPLETED, basis: KIND_COMPLETION_BASIS[request.kind], evidence: { effect, check } }
        : { outcome: OUTCOMES.UNKNOWN, basis: 'value-unverified', evidence: { effect, check } };
    }
    return { outcome: OUTCOMES.COMPLETED, basis: KIND_COMPLETION_BASIS[request.kind], evidence: { effect } };
  }

  async #awaitCompletion(workspaceId, completion, effect) {
    const probe = this.#probe(workspaceId);
    const deadline = this.#nowMs() + this.#config.dispatch.completionTimeoutMs;
    let last = null;
    for (;;) {
      if (completion.failedWhen?.length) {
        const failed = await evaluateConditions(completion.failedWhen, probe);
        if (failed.allEstablished) {
          return { outcome: OUTCOMES.FAILED, basis: 'failure-condition', evidence: { effect, conditions: failed } };
        }
      }
      last = await evaluateConditions(completion.completedWhen, probe);
      if (last.allEstablished) {
        return { outcome: OUTCOMES.COMPLETED, basis: 'completion-condition', evidence: { effect, conditions: last } };
      }
      if (this.#nowMs() >= deadline) break;
      await new Promise((resolve) => setTimeout(resolve, this.#config.dispatch.completionPollMs));
    }
    return { outcome: OUTCOMES.UNKNOWN, basis: 'completion-unestablished', evidence: { effect, conditions: last } };
  }

  #describe(workspace, actionId) {
    return { ...describeAction(workspace.actions.get(actionId)), boundary: NONATOMIC_BOUNDARY };
  }

  #outstanding(workspace) {
    return [...workspace.actions.values()]
      .map(describeAction)
      .filter((action) => action.isUncertain
        || action.dispatchStatus === ACTION_STATUS.QUEUED
        || (action.dispatchStatus === ACTION_STATUS.DISPATCHED && action.outcome === null)
        || action.acknowledgedGeneration === null);
  }

  async outstanding(command) {
    const workspace = this.#workspace(command.workspaceId);
    authenticateOwner(workspace, command);
    return this.#outstanding(workspace);
  }

  async action(command) {
    const workspace = this.#workspace(command.workspaceId);
    authenticateOwner(workspace, command);
    if (!workspace.actions.has(command.actionId)) {
      throw workspaceError(REJECTION_CODES.UNKNOWN_ACTION, String(command.actionId), { actionId: command.actionId });
    }
    return this.#describe(workspace, command.actionId);
  }

  /** Record that the current owner received an action's settled state. */
  async acknowledge(command) {
    const workspace = this.#workspace(command.workspaceId);
    return this.#runtimeFor(command.workspaceId).mutex.run(() => {
      authenticateOwner(workspace, command);
      const action = workspace.actions.get(command.actionId);
      if (!action) {
        throw workspaceError(REJECTION_CODES.UNKNOWN_ACTION, String(command.actionId), { actionId: command.actionId });
      }
      if (action.status === ACTION_STATUS.QUEUED || (action.status === ACTION_STATUS.DISPATCHED && !action.outcome)) {
        throw workspaceError(REJECTION_CODES.INVALID_COMMAND, 'action has not settled', { actionId: command.actionId });
      }
      this.#append(RECORD_TYPES.ACTION_ACKNOWLEDGED, {
        workspaceId: command.workspaceId,
        actionId: command.actionId,
        ownerGeneration: workspace.ownerGeneration,
      });
      return this.#describe(workspace, command.actionId);
    });
  }

  #requireUnknown(workspace, actionId) {
    const action = workspace.actions.get(actionId);
    if (!action) throw workspaceError(REJECTION_CODES.UNKNOWN_ACTION, String(actionId), { actionId });
    if (action.outcome?.outcome !== OUTCOMES.UNKNOWN || describeAction(action).isUncertain === false) {
      throw workspaceError(REJECTION_CODES.RECONCILIATION_NOT_APPLICABLE, `${actionId} is not uncertain`, { actionId });
    }
    return action;
  }

  /**
   * Resolve or route an unknown outcome. Application-state reconciliation reads the live page;
   * external idempotency permits a resubmission reusing the original key; human review defers
   * to the workspace administrator. The unknown outcome record itself is never rewritten.
   */
  async reconcile(command) {
    const workspace = this.#workspace(command.workspaceId);
    if (!this.#config.reconciliationMethods.includes(command.method)) {
      throw workspaceError(REJECTION_CODES.UNSUPPORTED_RECONCILIATION, String(command.method), {
        expected: this.#config.reconciliationMethods,
        received: command.method,
      });
    }
    if (command.method === 'application-state') {
      validateConditions(command.effectPresentWhen ?? [], this.#config.conditionKinds, 'effectPresentWhen');
      validateConditions(command.effectAbsentWhen ?? [], this.#config.conditionKinds, 'effectAbsentWhen');
      await this.#page(command.workspaceId);
    }
    return this.#runtimeFor(command.workspaceId).mutex.run(async () => {
      authenticateOwner(workspace, command);
      const action = this.#requireUnknown(workspace, command.actionId);
      const body = await this.#reconciliationBody(workspace, action, command);
      this.#append(RECORD_TYPES.ACTION_RECONCILED, {
        workspaceId: command.workspaceId,
        actionId: command.actionId,
        method: command.method,
        ownerGeneration: workspace.ownerGeneration,
        decidedBy: { ownerId: workspace.ownerId },
        ...body,
      });
      return this.#describe(workspace, command.actionId);
    });
  }

  async #reconciliationBody(workspace, action, command) {
    if (command.method === 'external-idempotency') {
      const key = action.request.effect?.externalIdempotencyKey;
      if (!key) {
        throw workspaceError(REJECTION_CODES.RECONCILIATION_NOT_APPLICABLE, 'action carried no external idempotency key', {
          actionId: command.actionId,
        });
      }
      return {
        result: 'idempotent-resubmission-permitted',
        resolvesUncertainty: false,
        permitsResubmission: true,
        requiredIdempotencyKey: key,
        basis: { externalIdempotencyKey: key },
      };
    }
    if (command.method === 'human-review') {
      return { result: 'human-review-requested', resolvesUncertainty: false, permitsResubmission: false, basis: {} };
    }
    const probe = this.#probe(workspace.workspaceId);
    const present = await evaluateConditions(command.effectPresentWhen ?? [], probe);
    const absent = await evaluateConditions(command.effectAbsentWhen ?? [], probe);
    const isPresent = (command.effectPresentWhen ?? []).length > 0 && present.allEstablished;
    const isAbsent = (command.effectAbsentWhen ?? []).length > 0 && absent.allEstablished;
    const result = isPresent === isAbsent ? 'inconclusive' : (isPresent ? 'effect-present' : 'effect-absent');
    return {
      result,
      resolvesUncertainty: result !== 'inconclusive',
      permitsResubmission: result === 'effect-absent',
      basis: { url: await probe.url(), effectPresent: present, effectAbsent: absent },
    };
  }

  /** Administrator decision on a human-review request. */
  async decideHumanReview({ workspaceId, adminCredential, actionId, decision, note = '' }) {
    const workspace = this.#workspace(workspaceId);
    authenticateAdmin(workspace, adminCredential);
    const decisions = { 'effect-present': false, 'effect-absent': true };
    if (!(decision in decisions)) {
      throw workspaceError(REJECTION_CODES.INVALID_COMMAND, 'decision: expected effect-present or effect-absent', {
        path: 'decision',
        received: decision,
      });
    }
    return this.#runtimeFor(workspaceId).mutex.run(() => {
      const action = this.#requireUnknown(workspace, actionId);
      if (action.reconciliations.at(-1)?.result !== 'human-review-requested') {
        throw workspaceError(REJECTION_CODES.RECONCILIATION_NOT_APPLICABLE, 'no pending human review', { actionId });
      }
      this.#append(RECORD_TYPES.ACTION_RECONCILED, {
        workspaceId,
        actionId,
        method: 'human-review',
        ownerGeneration: workspace.ownerGeneration,
        decidedBy: { role: 'workspace-admin' },
        result: decision,
        resolvesUncertainty: true,
        permitsResubmission: decisions[decision],
        basis: { note },
      });
      return this.#describe(workspace, actionId);
    });
  }

  /** Stop accepting commands and release the browser and journal. */
  async close() {
    this.#ready = false;
    await this.#adapter.close();
    this.#journal.close();
  }
}

export { NONATOMIC_BOUNDARY, WorkspaceService };
