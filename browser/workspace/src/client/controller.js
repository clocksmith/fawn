// Human and agent clients compose the supported service; only the service dispatches effects.
import { randomUUID } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync, renameSync, existsSync, chmodSync, openSync, fsyncSync, closeSync } from 'node:fs';
import { join } from 'node:path';
import { realpath } from 'node:fs/promises';
import { InvestigationStore } from './investigation.js';
import { repositoryEvidence, repositoryFiles } from './repository.js';
import { startAgent, investigationContext } from './agent.js';
import { createOwnerSession } from '../workflows/owner-session.js';
import { sha256Hex } from '../journal.js';

export class WorkspaceController {
  constructor({ service, stateDir, config, planner = null, workflowStore = null, workflowTasks = new Map() }) {
    this.service = service; this.config = config; this.planner = planner; this.active = new Map(); this.owners = new Map(); this.operations = new Map();
    this.workflowStore = workflowStore;
    this.workflowTasks = new Map(workflowTasks);
    mkdirSync(stateDir, { recursive: true, mode: 0o700 }); chmodSync(stateDir, 0o700);
    this.credentialsPath = join(stateDir, 'client-credentials.json');
    this.credentials = existsSync(this.credentialsPath) ? JSON.parse(readFileSync(this.credentialsPath, 'utf8')) : {};
    this.store = new InvestigationStore(join(stateDir, 'investigations'));
  }
  saveCredentials() {
    const temporary = `${this.credentialsPath}.new`;
    writeFileSync(temporary, `${JSON.stringify(this.credentials)}\n`, { mode: 0o600 });
    chmodSync(temporary, 0o600);
    const fd = openSync(temporary, 'r');
    try { fsyncSync(fd); } finally { closeSync(fd); }
    renameSync(temporary, this.credentialsPath);
  }
  admin(id) { const value = this.credentials[id]; if (!value) throw Error('Missing investigation admin credential'); return { workspaceId: id, adminCredential: value }; }
  async restore() {
    for (const id of this.store.projects.keys()) {
      const interrupted = this.store.get(id).run;
      await this.takeOver(id);
      if (interrupted?.status === 'running') this.store.run(id, { ...interrupted,
        status: 'needs-attention', detail: 'Service restarted. Review recorded outcomes before explicitly resuming.' });
    }
  }
  async create({ goal, repositoryRoot = null }) {
    if (typeof goal !== 'string' || !goal.trim() || Buffer.byteLength(goal) > this.config.maxInputBytes) throw Error('Provide a non-empty goal within the investigation input budget');
    const root = repositoryRoot ? await realpath(repositoryRoot) : null;
    const id = `investigation-${randomUUID()}`;
    const { adminCredential } = await this.service.createWorkspace({ workspaceId: id, scopedReads: true });
    this.credentials[id] = adminCredential; this.saveCredentials();
    this.store.create({ id, goal, repositoryRoot: root });
    await this.takeOver(id); return { id };
  }
  owner(id) { const value = this.owners.get(id); if (!value) throw Error('Investigation has no current client owner'); return { workspaceId: id, ...value }; }
  exclusive(id, task) {
    const previous = this.operations.get(id) ?? Promise.resolve();
    const result = previous.then(task);
    const settled = result.then(() => {}, () => {});
    this.operations.set(id, settled);
    settled.then(() => { if (this.operations.get(id) === settled) this.operations.delete(id); });
    return result;
  }
  takeOver(id) { return this.exclusive(id, () => this.takeOverNow(id)); }
  async takeOverNow(id) {
    this.active.get(id)?.controller.abort(Error('Human took control'));
    const owner = await this.service.assignOwner({ ...this.admin(id), ownerId: 'human' });
    this.owners.set(id, createOwnerSession(this.service, { workspaceId: id, ...owner }).identity);
    const run = this.store.get(id).run;
    if (run?.status === 'running') this.store.run(id, { ...run, status: 'stopped', detail: 'Human has control. Previously dispatched work may still complete.' });
    this.store.activity(id, { event: 'human-control', outstanding: owner.outstanding });
    return { outstanding: owner.outstanding, boundary: owner.boundary };
  }
  async humanPage(id, pageId, extraOrigins = []) {
    if (this.owner(id).ownerId !== 'human') throw Error('Take over before issuing a human action');
    const state = await this.service.inspect(this.admin(id));
    const page = state.pages.find(value => value.pageId === pageId && !value.closed);
    if (!page) throw Error('Select an open page');
    const origin = page.url === 'about:blank' ? [] : [new URL(page.url).origin];
    await this.service.grantAuthorization({ ...this.admin(id), grantId: `human-${pageId}`, ownerId: 'human',
      intents: ['human'], actionKinds: ['navigate', 'click', 'fill', 'select', 'submit', 'download'],
      targetOrigins: [...new Set([...origin, ...extraOrigins, ...(origin.length || extraOrigins.length ? [] : ['about:blank'])])], pageIds: [pageId], readRegions: null });
    return this.owner(id);
  }
  async navigate(id, { pageId, url }) {
    const target = new URL(url);
    if (!['http:', 'https:'].includes(target.protocol)) throw Error('Navigation requires HTTP or HTTPS');
    const owner = await this.humanPage(id, pageId, [target.origin]);
    const observation = await this.service.observe({ ...owner, pageId });
    const admitted = await this.service.submitAction({ ...owner, pageId, actionId: randomUUID(), authorizedIntent: 'human',
      kind: 'navigate', target: { url: target.href }, observationVersion: observation.observationVersion });
    return admitted.result;
  }
  async capture(id, { pageId, regions = [], artifactId, expectedRevision = 0 }, owner = null) {
    const identity = owner ?? await this.humanPage(id, pageId);
    const observation = await this.service.observe({ ...identity, pageId, ...(regions.length ? { regions } : {}) });
    const content = observation.text;
    const source = { kind: 'page', pageId, url: observation.url, observationVersion: observation.observationVersion,
      capturedAt: new Date().toISOString(), regions: observation.readScope?.regions ?? null, content, sha256: sha256Hex(content) };
    const previous = artifactId ? this.store.get(id).artifacts[artifactId] : null;
    if (artifactId && (previous?.source?.kind !== 'page' || previous.source.pageId !== pageId)) throw Error('Source identity does not match this page');
    if (previous && observation.url === 'about:blank') throw Error('Reopen the source page before refreshing its evidence');
    if (previous?.source.sha256 === source.sha256 && previous.source.url === source.url) return previous;
    return this.store.revise(id, { id: artifactId, expectedRevision, kind: 'evidence', text: `Page evidence: ${observation.title || observation.url}`, source });
  }
  async readRepository(id, { path, artifactId, expectedRevision = 0 }) {
    const source = await repositoryEvidence(this.store.get(id).repositoryRoot, path, this.config.maxEvidenceBytes);
    const previous = artifactId ? this.store.get(id).artifacts[artifactId] : null;
    if (artifactId && (previous?.source?.kind !== 'repository' || previous.source.path !== source.path)) throw Error('Source identity does not match this repository file');
    if (previous?.source.sha256 === source.sha256) return previous;
    return this.store.revise(id, { id: artifactId, expectedRevision, kind: 'evidence', text: `Repository evidence: ${path}`, source });
  }
  record(id, input) {
    if (input.kind === 'evidence' || (input.id && this.store.get(id).artifacts[input.id]?.kind === 'evidence')) throw Error('Capture a page or repository file to create evidence');
    return this.store.revise(id, input);
  }
  /** Trusted host entrypoint. Task/account/disclosure/oracle definitions are installed by the
   * host, not accepted from HTTP, a model, or a downloaded graph. No final-submit permission.
   */
  runPersonalWorkflow(id, { template, pageId, inputs, mode }) {
    return this.exclusive(id, async () => {
      const definition = this.workflowTasks.get(template);
      if (!this.workflowStore || !definition || !['learn', 'reuse', 'fresh'].includes(mode)) {
        throw Error('Select an installed personal workflow and explicit mode');
      }
      if (this.active.has(id)) throw Error('The prior agent has not settled');
      const before = await this.service.inspect(this.admin(id));
      if (before.actions.some(action => action.isUncertain || action.dispatchStatus === 'queued'
        || (action.dispatchStatus === 'dispatched' && !action.outcome))) throw Error('Review unresolved actions before delegating');
      const grantId = `workflow-${randomUUID()}`, runId = randomUUID();
      const { disclosurePolicy, accountScope, verifyAccount, verifyReview, verificationId, taskSourceHashes, planner, task, fieldGroups = null } = definition;
      const profile = structuredClone(inputs);
      await this.service.grantAuthorization({ ...this.admin(id), grantId, ownerId: 'agent',
        intents: ['personal-workflow'], targetOrigins: [disclosurePolicy.origin],
        actionKinds: ['fill', 'select', 'click'], pageIds: [pageId], readRegions: null,
        expiresAt: new Date(Date.now() + this.config.delegationMaxAgeMs).toISOString() });
      for (const grant of before.grants.filter(value => value.ownerId === 'agent')) {
        await this.service.revokeAuthorization({ ...this.admin(id), grantId: grant.grantId });
      }
      const assigned = await this.service.assignOwner({ ...this.admin(id), ownerId: 'agent' });
      const owner = createOwnerSession(this.service, { workspaceId: id, ...assigned }).identity;
      this.owners.set(id, owner);
      const controller = new AbortController();
      this.store.activity(id, { event: 'personal-workflow-started', template, mode, grantId });
      const done = this.workflowStore.run({ template, mode, pageId, inputs: profile,
        accountScope, verifyAccount, verificationId, taskSourceHashes, disclosurePolicy, fieldGroups, service: this.service, owner,
        intent: 'personal-workflow', task, planner, config: this.config, store: this.store,
        projectId: id, signal: controller.signal,
        verifyReview: () => verifyReview({ service: this.service, owner, pageId, inputs: profile }),
      }).then(result => {
        this.store.activity(id, { event: 'personal-workflow-finished', completed: result.completed,
          persistence: result.persistence, modelCalls: result.modelCalls, cacheSteps: result.cacheSteps });
        return result;
      }).finally(() => this.exclusive(id, async () => {
        await this.service.revokeAuthorization({ ...this.admin(id), grantId });
        if (this.owners.get(id)?.ownerGeneration === owner.ownerGeneration) await this.takeOverNow(id);
      }));
      this.active.set(id, { id: runId, done, controller });
      const release = () => { if (this.active.get(id)?.id === runId) this.active.delete(id); };
      done.then(release, release);
      return { runId, done };
    });
  }
  delegate(id, input) { return this.exclusive(id, () => this.delegateNow(id, input)); }
  async delegateNow(id, { pageIds, origins, regions = null, actionKinds, task, shareRecords = false, uploadFiles }) {
    if (!this.planner) throw Error('Configure FAWN_MODEL and the planner API credential before delegating');
    if (!Array.isArray(pageIds) || !pageIds.length || !Array.isArray(origins) || !origins.length) throw Error('Select pages and exact origins');
    if (!Array.isArray(actionKinds) || !actionKinds.length || typeof task !== 'string' || !task.trim()) throw Error('Select actions and provide a bounded task');
    if (this.active.has(id)) throw Error('The prior agent has not settled; take over and wait before resuming');
    const before = await this.service.inspect(this.admin(id));
    if (before.actions.some(action => action.isUncertain || (action.dispatchStatus === 'dispatched' && !action.outcome))) throw Error('Review unresolved actions before delegating');
    const grantId = `delegation-${randomUUID()}`;
    const approved = await this.service.grantAuthorization({ ...this.admin(id), grantId, ownerId: 'agent', intents: ['delegated-task'],
      targetOrigins: origins, actionKinds, pageIds, readRegions: regions,
      ...(uploadFiles !== undefined ? { uploadFiles } : {}),
      expiresAt: new Date(Date.now() + this.config.delegationMaxAgeMs).toISOString() });
    // A new task must not inherit an earlier delegation's wider access.
    for (const grant of before.grants.filter(value => value.ownerId === 'agent')) await this.service.revokeAuthorization({ ...this.admin(id), grantId: grant.grantId });
    const assigned = await this.service.assignOwner({ ...this.admin(id), ownerId: 'agent' });
    const owner = createOwnerSession(this.service, { workspaceId: id, ...assigned }).identity;
    this.owners.set(id, owner);
    const controller = new AbortController(), observations = new Map();
    const initialArtifacts = new Set(Object.keys(this.store.get(id).artifacts));
    const visibleArtifacts = new Set(shareRecords ? initialArtifacts : []);
    const startSequence = this.store.journal.records.at(-1)?.seq ?? 0;
    this.store.activity(id, { event: 'delegated', task, pageIds, origins, regions, actionKinds, shareRecords, grantId,
      ...(approved.uploadFiles ? { uploadFiles: approved.uploadFiles } : {}) });
    const assertCurrent = () => { if (this.owners.get(id)?.ownerGeneration !== owner.ownerGeneration) throw Error('Agent ownership changed'); };
    const execute = async (tool, input, { callId }) => {
      assertCurrent(); controller.signal.throwIfAborted();
      const pageId = input.pageId ?? pageIds[0];
      if (['observe', 'act', 'capture'].includes(tool) && !pageIds.includes(pageId)) throw Error('Page exceeds this delegation');
      if (tool === 'observe') {
        const observation = await this.service.observe({ ...owner, pageId }); observations.set(pageId, observation); return observation;
      }
      if (input.artifactId && !visibleArtifacts.has(input.artifactId)) throw Error('Source record was not shared with this task');
      if (tool === 'capture') { const artifact = await this.capture(id, { ...input, pageId }, owner); visibleArtifacts.add(artifact.id); return artifact; }
      if (tool === 'repository_files') return repositoryFiles(this.store.get(id).repositoryRoot, this.config.maxRepositoryFiles);
      if (tool === 'repository_read') { const artifact = await this.readRepository(id, input); visibleArtifacts.add(artifact.id); return artifact; }
      if (tool === 'record') {
        if ((input.id && !visibleArtifacts.has(input.id)) || input.dependsOn?.some(key => !visibleArtifacts.has(key))) throw Error('Reasoning depends on records not shared with this task');
        const artifact = this.record(id, input); visibleArtifacts.add(artifact.id); return artifact;
      }
      if (tool !== 'act') throw Error(`Unsupported product tool ${tool}`);
      const observed = observations.get(pageId);
      if (!observed) throw Error('Observe this page before proposing an action');
      const admitted = await this.service.submitAction({ ...owner, pageId, actionId: callId, authorizedIntent: 'delegated-task',
        kind: input.kind, target: input.target, params: input.params, completion: input.completion, effect: input.effect,
        observationVersion: observed.observationVersion });
      observations.delete(pageId); return admitted.result;
    };
    const run = startAgent({ projectId: id, store: this.store, planner: this.planner, config: this.config,
      execute, signal: controller.signal, assertCurrent, context: () => investigationContext(this.store.get(id), {
        task, permissions: { pageIds, origins, regions, actionKinds,
          ...(approved.uploadFiles ? { uploadFiles: approved.uploadFiles } : {}) }, initialArtifacts, startSequence, shareRecords, visibleArtifacts
      }) });
    const settled = run.done.then(async () => {
      await this.exclusive(id, async () => {
        await this.service.revokeAuthorization({ ...this.admin(id), grantId });
        if (this.owners.get(id)?.ownerGeneration === owner.ownerGeneration) await this.takeOverNow(id);
      });
    });
    this.active.set(id, { ...run, done: settled, controller });
    const release = () => { if (this.active.get(id)?.id === run.id) this.active.delete(id); };
    settled.then(release, release);
    return { runId: run.id };
  }
  revoke(id, { grantId }) {
    return this.exclusive(id, async () => {
      await this.service.revokeAuthorization({ ...this.admin(id), grantId });
      return this.takeOverNow(id);
    });
  }
  async check(id, { actionId }) {
    const owner = await this.takeOver(id);
    const action = await this.service.action({ ...this.owner(id), actionId });
    await this.humanPage(id, action.pageId);
    if (!action.isUncertain) return { action, boundary: owner.boundary };
    const conditions = action.completion?.completedWhen ?? [];
    if (!conditions.length) return this.service.reconcile({ ...this.owner(id), actionId, method: 'human-review' });
    return this.service.reconcile({ ...this.owner(id), actionId, method: 'application-state', effectPresentWhen: conditions });
  }
  async status(id = null) {
    const projects = [...this.store.projects.keys()].map(key => this.store.get(key));
    const exists = id && this.store.projects.has(id);
    return { projects, missingProjectId: id && !exists ? id : null, selected: exists ? { investigation: this.store.get(id), workspace: await this.service.inspect(this.admin(id)) } : null,
      planner: this.planner?.identity ?? null, pollMs: this.config.pollMs };
  }
  async close() {
    for (const id of this.active.keys()) await this.takeOver(id);
    await Promise.all([...this.active.values()].map(value => value.done));
    this.store.close();
  }
}
