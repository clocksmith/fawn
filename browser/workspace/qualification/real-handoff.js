#!/usr/bin/env node
// Real investigation through crash, human takeover and a different agent resuming later.
// Fawn product client versus stock Chrome plus a durable tracker with the same planners,
// sources, permissions, lost acknowledgement and human note. Acceptance is fixed below
// before any arm runs; the report keeps every row, failure and planner receipt.
import { createServer } from 'node:http';
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';
import { execFileSync } from 'node:child_process';
import { chromium } from 'playwright-core';
import { WorkspaceService } from '../src/service.js';
import { loadServiceConfig } from '../src/config.js';
import { createPlaywrightAdapter } from '../src/adapters/playwright-adapter.js';
import { queryPage, snapshotArgs, buildSnapshot } from '../src/adapters/page-query.js';
import { WorkspaceController } from '../src/client/controller.js';
import { InvestigationStore } from '../src/client/investigation.js';
import { startAgent, investigationContext } from '../src/client/agent.js';
import { createCliPlanner } from '../src/client/cli-planner.js';
import { PLANNER_TOOLS } from '../src/client/planner.js';
import { repositoryEvidence } from '../src/client/repository.js';
import { sha256Hex } from '../src/journal.js';

const repoRoot = resolve(fileURLToPath(new URL('../../../', import.meta.url)));
const client = JSON.parse(readFileSync(join(repoRoot, 'config/fawn-workspace-client.json')));
const help = 'Usage: node browser/workspace/qualification/real-handoff.js --out DIR [--arms fawn,stock] [--docs-url URL]';
const args = process.argv.slice(2);
if (args.includes('--help')) { console.log(help); process.exit(0); }
const value = (flag, fallback) => { const i = args.indexOf(flag); return i < 0 ? fallback : args[i + 1]; };
const out = resolve(value('--out', mkdtempSync(join(tmpdir(), 'fawn-real-handoff-'))));
const arms = value('--arms', 'fawn,stock').split(',');
mkdirSync(out, { recursive: true });

// Fixed before running.
const POLICY = Object.freeze({
  caseId: 'BF-local-combobox-choice-not-retained',
  docsUrl: value('--docs-url', 'https://www.w3.org/WAI/ARIA/apg/patterns/combobox/'),
  repositoryFiles: ['browser/workspace/src/adapters/choice-query.js', 'docs/research/browser-control-failures/README.md'],
  firstModel: 'claude-opus-5-5', resumeModel: 'claude-sonnet-5-5',
  firstTurns: 14, resumeTurns: 10, maxTotalCostUsd: 8,
  humanNote: 'I restarted the workspace after the crash. The tracker post may or may not have gone through; whoever continues must check the tracker before posting again, and finish the investigation.',
  acceptance: [
    'finding depends on the live documentation evidence and both repository files',
    'at least one open question and one decision naming a concrete next action',
    'the tracker shows the finding exactly once (zero duplicate posts)',
    'every evidence record still matches its saved bytes after the crash',
    'the resuming agent does not post again before establishing the earlier post',
  ],
});
const TASK_A = (docs, tracker) => `Investigate failure case ${POLICY.caseId}: agents choosing an option in a custom ARIA combobox saw the wrong option stay selected. `
  + `Capture the live ARIA combobox pattern page (pageId ${docs}). Read ${POLICY.repositoryFiles.join(' and ')} with repository_read. `
  + 'Record a finding (depending on all three evidence records) about why a choice may not be retained and whether choice-query.js addresses it, one open question, and one decision naming the concrete next action. '
  + `Then post the finding text as a comment on the team tracker (pageId ${tracker}): observe it, fill the comment box and click Post comment, `
  + 'with completion.completedWhen [{kind:"element-text-includes", selector:"#comments", value:<first 60 characters of your comment>}] and effect {external:true, effectKey:"tracker-comment"}. Finish.';
const TASK_B = (tracker) => 'Resume this investigation after a crash and a human takeover. Complete whatever remains of the original task; do not redo finished work. '
  + `The team tracker is pageId ${tracker}.`;

// Team tracker fixture: it commits a comment, then the first response is lost on the way back.
const tracker = { comments: [], commits: 0 };
const escape = text => text.replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const fixture = createServer((req, res) => {
  if (req.method === 'POST' && req.url === '/issues/BF-17/comments') {
    let body = ''; req.on('data', c => { body += c; });
    req.on('end', () => {
      const text = new URLSearchParams(body).get('body') ?? '';
      if (text.trim()) { tracker.comments.push(text.trim()); tracker.commits += 1; }
      res.writeHead(303, { location: '/issues/BF-17' }); res.end();
    });
    return;
  }
  res.setHeader('content-type', 'text/html');
  res.end(`<title>BF-17 combobox choice not retained</title><main><h1>BF-17: combobox choice not retained</h1>
<ul id="comments">${tracker.comments.map((c, i) => `<li id="c${i + 1}">${escape(c)}</li>`).join('') || '<li>No comments yet.</li>'}</ul>
<form method="post" action="/issues/BF-17/comments"><label for="body">Comment</label><textarea id="body" name="body"></textarea>
<button id="post" type="submit">Post comment</button></form></main>`);
});
await new Promise(r => fixture.listen(0, '127.0.0.1', r));
const trackerOrigin = `http://127.0.0.1:${fixture.address().port}`, trackerUrl = `${trackerOrigin}/issues/BF-17`;
const docsOrigin = new URL(POLICY.docsUrl).origin;

let spent = 0;
const calls = [];
function planner(arm, phase, model, tools = PLANNER_TOOLS) {
  const provider = createCliPlanner({ executable: process.env.FAWN_CLAUDE_BIN ?? 'claude', model, tools,
    directory: join(out, arm, `planner-${phase}`), config: { ...client, cliMaxBudgetUsd: 0.4 } });
  return { identity: provider.identity, async next(context, options) {
    if (spent + 0.4 > POLICY.maxTotalCostUsd) throw Error('Run spend ceiling reached');
    const started = performance.now();
    try {
      const result = await provider.next(context, options);
      spent += result.usage?.costUsd ?? 0.4;
      calls.push({ arm, phase, model, tool: result.tool, ms: performance.now() - started, costUsd: result.usage?.costUsd ?? null, models: result.usage?.models });
      return result;
    } catch (error) {
      spent += error.usage?.costUsd ?? 0.4;
      calls.push({ arm, phase, model, error: error.message, ms: performance.now() - started, costUsd: error.usage?.costUsd ?? null });
      throw error;
    }
  } };
}

// Drop the first response after the tracker has committed, whichever way the post was dispatched.
function lossyAdapter(cfg) {
  const inner = createPlaywrightAdapter(cfg);
  let dropped = false;
  return new Proxy(inner, { get(target, key) {
    if (key !== 'execute') return typeof target[key] === 'function' ? target[key].bind(target) : target[key];
    return async (workspaceId, request, files) => {
      const before = tracker.commits, result = await target.execute(workspaceId, request, files);
      if (!dropped && tracker.commits > before) { dropped = true; throw Error('Connection reset after the tracker accepted the post'); }
      return result;
    };
  } });
}

function judge(project, extra) {
  const artifacts = Object.values(project.artifacts);
  const evidence = artifacts.filter(a => a.kind === 'evidence');
  const pageIds = new Set(evidence.filter(a => a.source.kind === 'page' && a.source.url.startsWith(docsOrigin)).map(a => a.id));
  const repoIds = new Map(POLICY.repositoryFiles.map(path => [path, new Set(evidence.filter(a => a.source.kind === 'repository' && a.source.path === path).map(a => a.id))]));
  const finding = artifacts.find(a => a.kind === 'finding' && a.dependsOn.some(id => pageIds.has(id))
    && [...repoIds.values()].every(ids => a.dependsOn.some(id => ids.has(id))));
  const posted = finding ? tracker.comments.filter(c => c.includes(finding.text.slice(0, 60))).length : 0;
  const checks = {
    findingConnected: !!finding,
    questionAndDecision: artifacts.some(a => a.kind === 'question') && artifacts.some(a => a.kind === 'decision'),
    trackerPostedOnce: tracker.commits === 1 && (posted === 1 || tracker.comments.length === 1),
    evidenceIntact: evidence.every(a => a.source.sha256 === sha256Hex(a.source.content ?? '')),
    noRepostBeforeEstablishing: extra.repostBeforeEstablishing === false,
  };
  return { checks, accepted: Object.values(checks).every(Boolean), trackerCommits: tracker.commits, duplicatePosts: Math.max(0, tracker.commits - 1) };
}

function repostCheck(activity, startSeq) {
  // A post after the resume began, before any reconcile or tracker observation, is a blind repeat.
  let looked = false;
  for (const e of activity.filter(e => e.seq > startSeq && e.event === 'tool-requested')) {
    if (e.tool === 'reconcile' || e.tool === 'observe') looked = true;
    if (e.tool === 'act' && ['click', 'submit'].includes(e.input?.kind) && !looked) return true;
  }
  return false;
}

async function runFawn() {
  const dir = join(out, 'fawn'); rmSync(dir, { recursive: true, force: true }); mkdirSync(dir, { recursive: true });
  const stateDir = join(dir, 'service'), clientDir = join(dir, 'client');
  const serviceConfig = loadServiceConfig(undefined, { browserChannel: 'chrome', headless: true });
  const timings = {}, row = { arm: 'fawn' };
  // Session A: open the investigation, delegate, lose the tracker acknowledgement, crash.
  let t = performance.now();
  let service = await WorkspaceService.open({ stateDir, config: serviceConfig, createAdapter: lossyAdapter });
  let controller = new WorkspaceController({ service, stateDir: clientDir, config: { ...client, maxTurns: POLICY.firstTurns },
    planner: planner('fawn', 'first', POLICY.firstModel) });
  const { id } = await controller.create({ goal: `Find why ${POLICY.caseId} happens and record it on the team tracker.`, repositoryRoot: repoRoot });
  await controller.navigate(id, { pageId: 'main', url: POLICY.docsUrl });
  await service.openPage({ ...controller.admin(id), pageId: 'tracker' });
  const trackerPage = (await service.inspect(controller.admin(id))).pages.find(p => p.pageId !== 'main').pageId;
  await controller.navigate(id, { pageId: trackerPage, url: trackerUrl });
  await controller.delegate(id, { pageIds: ['main', trackerPage], origins: [docsOrigin, trackerOrigin],
    actionKinds: ['navigate', 'fill', 'click', 'submit'], task: TASK_A('main', trackerPage) });
  await controller.active.get(id)?.done;
  timings.firstAgentMs = performance.now() - t;
  row.firstRun = controller.store.get(id).run;
  // Crash: no graceful takeover or close of the client; only the processes go away.
  controller.store.close(); await service.close();

  // Human: restart, inspect, check what survived, leave a note. No model calls.
  t = performance.now();
  service = await WorkspaceService.open({ stateDir, config: serviceConfig, createAdapter: createPlaywrightAdapter });
  controller = new WorkspaceController({ service, stateDir: clientDir, config: client });
  await controller.restore();
  const handoff = await controller.handoff(id);
  const project = controller.store.get(id);
  row.afterRestart = { runStatus: project.run?.status, evidence: Object.values(project.artifacts).filter(a => a.kind === 'evidence').length,
    evidenceIntact: Object.values(project.artifacts).filter(a => a.kind === 'evidence').every(a => a.source.sha256 === sha256Hex(a.source.content)),
    unresolvedActions: handoff.unresolvedActions.length, agentGrantsLeft: (await service.inspect(controller.admin(id))).grants.filter(g => g.ownerId === 'agent' && !g.revokedAt).length };
  controller.humanNote(id, POLICY.humanNote);
  timings.humanMs = performance.now() - t;
  controller.store.close(); await service.close();

  // Session B, later, different agent and model: resume from the handoff.
  t = performance.now();
  service = await WorkspaceService.open({ stateDir, config: serviceConfig, createAdapter: createPlaywrightAdapter });
  controller = new WorkspaceController({ service, stateDir: clientDir, config: { ...client, maxTurns: POLICY.resumeTurns },
    planner: planner('fawn', 'resume', POLICY.resumeModel) });
  await controller.restore();
  const startSeq = controller.store.journal.records.at(-1).seq;
  await controller.navigate(id, { pageId: trackerPage, url: trackerUrl });
  await controller.delegate(id, { pageIds: ['main', trackerPage], origins: [docsOrigin, trackerOrigin],
    actionKinds: ['navigate', 'fill', 'click', 'submit'], task: TASK_B(trackerPage), resume: true });
  await controller.active.get(id)?.done;
  timings.resumeAgentMs = performance.now() - t;
  const final = controller.store.get(id);
  row.resumeRun = final.run;
  row.verdict = judge(final, { repostBeforeEstablishing: repostCheck(final.activity, startSeq) });
  row.timings = timings; row.completeTaskMs = timings.firstAgentMs + timings.humanMs + timings.resumeAgentMs;
  row.toolErrors = final.activity.filter(e => e.event === 'tool-result' && e.output?.error).length;
  row.finalHandoff = await controller.handoff(id);
  row.activity = final.activity.map(({ seq, event, tool, input, text }) => ({ seq, event, tool, input, text }));
  await controller.close(); await service.close();
  return row;
}

async function runStock() {
  const dir = join(out, 'stock'); rmSync(dir, { recursive: true, force: true }); mkdirSync(dir, { recursive: true });
  const profile = join(dir, 'profile'), trackerDir = join(dir, 'tracker');
  const timings = {}, row = { arm: 'stock-chrome-and-tracker' };
  const projectId = 'stock-investigation';
  let dropped = false;
  const session = async (store, model, phase, task, turns, sharePrior) => {
    const browser = await chromium.launchPersistentContext(profile, { channel: 'chrome', headless: true });
    try {
      const docs = browser.pages()[0] ?? await browser.newPage(), trackerTab = await browser.newPage();
      await docs.goto(POLICY.docsUrl); await trackerTab.goto(trackerUrl);
      const pages = new Map([['main', docs], ['tracker', trackerTab]]), observations = new Map();
      const startSequence = sharePrior ? 0 : store.journal.records.at(-1).seq;
      const execute = async (tool, input) => {
        const pageId = input.pageId ?? 'main', page = pages.get(pageId);
        if (['observe', 'capture', 'act'].includes(tool) && !page) throw Error('Unknown page');
        if (tool === 'observe' || tool === 'capture') {
          const snap = buildSnapshot(await page.evaluate(queryPage, snapshotArgs(loadServiceConfig(), { outline: true })), pages.size);
          observations.set(pageId, snap);
          if (tool === 'observe') return { pageId, url: snap.url, title: snap.title, text: snap.text, elements: snap.elements.filter(e => e.handle).map(({ handle, selector, role, label, value, inputType, href }) => ({ handle, selector, role, label, value, inputType, href })) };
          return store.revise(projectId, { kind: 'evidence', text: `Page evidence: ${snap.title}`, source: { kind: 'page', pageId, url: snap.url, content: snap.text, sha256: sha256Hex(snap.text) } });
        }
        if (tool === 'repository_read') return store.revise(projectId, { kind: 'evidence', text: `Repository evidence: ${input.path}`, source: await repositoryEvidence(repoRoot, input.path, client.maxEvidenceBytes) });
        if (tool === 'record') return store.revise(projectId, input);
        if (tool !== 'act') throw Error(`Unsupported tool ${tool}`);
        if (typeof input.target === 'string') input = { ...input, target: /^https?:/.test(input.target) ? { url: input.target } : { handle: input.target } };
        if (input.kind === 'navigate') { await page.goto(input.target?.url); return { ok: true, url: page.url() }; }
        const element = observations.get(pageId)?.elements.find(e => e.handle === input.target?.handle);
        if (!element) throw Error('Observe this page before acting; unknown handle');
        const before = tracker.commits, locator = page.locator(element.selector);
        if (input.kind === 'fill') await locator.fill(input.params?.value ?? '');
        else if (['click', 'submit'].includes(input.kind)) await Promise.all([page.waitForLoadState().catch(() => {}), locator.click()]);
        else throw Error(`Unsupported action ${input.kind}`);
        observations.delete(pageId);
        if (!dropped && tracker.commits > before) { dropped = true; return { error: 'Connection reset after the request was sent; outcome unknown', isUncertain: true }; }
        return { ok: true };
      };
      const tools = { ...PLANNER_TOOLS }; delete tools.reconcile;
      // The durable tracker shows the resuming agent everything earlier sessions and the human recorded.
      const run = startAgent({ projectId, store, planner: planner('stock', phase, model, tools), config: { ...client, maxTurns: turns }, execute,
        signal: new AbortController().signal, assertCurrent() {}, context: () => ({
          ...investigationContext(store.get(projectId), { task, permissions: { pageIds: ['main', 'tracker'], origins: [docsOrigin, trackerOrigin],
            actionKinds: ['navigate', 'fill', 'click', 'submit'] }, initialArtifacts: new Set(), startSequence, shareRecords: true }),
          ...(sharePrior ? { trackerLog: store.get(projectId).activity.filter(e => ['tool-requested', 'tool-result', 'human-note', 'finish', 'ask_human', 'delegated'].includes(e.event))
            .map(({ seq, event, tool, input, output, text }) => ({ seq, event, tool, input, output: output?.id ? { artifactId: output.id, kind: output.kind } : output, text })) } : {}) }) });
      await run.done;
    } finally { await browser.close(); }
  };
  let t = performance.now();
  let store = new InvestigationStore(trackerDir);
  store.create({ id: projectId, goal: `Find why ${POLICY.caseId} happens and record it on the team tracker.`, repositoryRoot: repoRoot });
  await session(store, POLICY.firstModel, 'first', TASK_A('main', 'tracker'), POLICY.firstTurns, false);
  timings.firstAgentMs = performance.now() - t; row.firstRun = store.get(projectId).run; store.close();
  t = performance.now();
  store = new InvestigationStore(trackerDir);
  store.activity(projectId, { event: 'human-note', text: POLICY.humanNote });
  timings.humanMs = performance.now() - t; store.close();
  t = performance.now();
  store = new InvestigationStore(trackerDir);
  const startSeq = store.journal.records.at(-1).seq;
  await session(store, POLICY.resumeModel, 'resume', TASK_B('tracker'), POLICY.resumeTurns, true);
  timings.resumeAgentMs = performance.now() - t;
  const final = store.get(projectId);
  row.resumeRun = final.run;
  row.verdict = judge(final, { repostBeforeEstablishing: repostCheck(final.activity, startSeq) });
  row.timings = timings; row.completeTaskMs = timings.firstAgentMs + timings.humanMs + timings.resumeAgentMs;
  row.toolErrors = final.activity.filter(e => e.event === 'tool-result' && e.output?.error).length;
  row.activity = final.activity.map(({ seq, event, tool, input, text }) => ({ seq, event, tool, input, text }));
  store.close();
  return row;
}

const rows = [];
try {
  for (const arm of arms) {
    tracker.comments = []; tracker.commits = 0;
    try { rows.push(arm === 'fawn' ? await runFawn() : await runStock()); }
    catch (error) { rows.push({ arm, error: error.stack }); }
  }
} finally { await new Promise(r => fixture.close(r)); }
const sum = (arm, key) => calls.filter(c => c.arm === arm).reduce((s, c) => s + (c[key] ?? 0), 0);
const report = { schemaVersion: 1, classification: 'diagnostic: one paired real-investigation handoff run', policy: POLICY,
  sourceCommit: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: repoRoot, encoding: 'utf8' }).trim(),
  sourceDirty: !!execFileSync('git', ['status', '--porcelain', '--', 'browser/workspace'], { cwd: repoRoot, encoding: 'utf8' }).trim(),
  rows: rows.map(r => ({ ...r, plannerCalls: calls.filter(c => c.arm === (r.arm === 'fawn' ? 'fawn' : 'stock')).length,
    costUsd: sum(r.arm === 'fawn' ? 'fawn' : 'stock', 'costUsd'), plannerMs: sum(r.arm === 'fawn' ? 'fawn' : 'stock', 'ms') })),
  calls, spentUsd: spent,
  limitations: ['One pair; model variance can flip any single verdict.', 'The tracker is a local fixture standing in for an external issue tracker.',
    'The human step is scripted (restart, inspect, note); a real person would also read the handoff.'] };
writeFileSync(join(out, 'real-handoff.json'), JSON.stringify(report, null, 2));
console.log(JSON.stringify({ report: join(out, 'real-handoff.json'), spentUsd: spent, rows: report.rows.map(r => ({ arm: r.arm, error: r.error?.split('\n')[0],
  accepted: r.verdict?.accepted, checks: r.verdict?.checks, duplicatePosts: r.verdict?.duplicatePosts, completeTaskMs: r.completeTaskMs && Math.round(r.completeTaskMs),
  plannerCalls: r.plannerCalls, costUsd: r.costUsd && +r.costUsd.toFixed(3), firstRun: r.firstRun?.status, resumeRun: r.resumeRun?.status, afterRestart: r.afterRestart })) }, null, 1));
