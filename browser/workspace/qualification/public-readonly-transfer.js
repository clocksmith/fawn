#!/usr/bin/env node
// Strict read-only public-site transfer harness for Track C1.
//
// Evaluates whether Fawn's frozen TraceDag v3 transfers to an owner-selected external site
// without permitting any non-GET/HEAD mutation, form submission, or cross-origin write.
// In --controlled mode, exercises the read-only network interceptor and workflow transfer
// against a local read-only documentation-index fixture whose POST endpoints are verified blocked.

import { createServer } from 'node:http';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { WorkspaceService } from '../src/service.js';
import { loadServiceConfig } from '../src/config.js';
import { createPlaywrightAdapter } from '../src/adapters/playwright-adapter.js';
import { WorkflowStore } from '../src/client/workflow-store.js';
import { InvestigationStore } from '../src/client/investigation.js';
import { createCliPlanner } from '../src/client/cli-planner.js';
import { WORKFLOW_SYSTEM, WORKFLOW_TOOLS } from '../src/client/workflow-agent.js';

export const ALLOWED_READONLY_METHODS = new Set(['GET', 'HEAD', 'OPTIONS']);

export function isReadOnlyHttpMethod(method) {
  return ALLOWED_READONLY_METHODS.has(String(method ?? '').toUpperCase());
}

export function validateReadOnlyTransferSpec(spec) {
  if (!spec || typeof spec !== 'object') throw Error('Read-only transfer spec must be an object');
  const url = new URL(spec.entryUrl);
  if (!['http:', 'https:'].includes(url.protocol)) {
    throw Error(`Unsupported entryUrl protocol: ${url.protocol}`);
  }
  if (!Array.isArray(spec.fields) || !Array.isArray(spec.intermediateClicks)) {
    throw Error('Spec requires explicit fields and intermediateClicks disclosure arrays');
  }
  for (const field of spec.fields) {
    const inputType = field.locator?.[3] ?? '';
    if (['submit', 'image', 'password', 'file', 'hidden'].includes(inputType)) {
      throw Error(`Forbidden input type in read-only transfer spec: ${inputType}`);
    }
  }
  if (!Array.isArray(spec.queries) || spec.queries.length < 2) {
    throw Error('Spec requires at least 2 read-only queries (1 learn + >=1 transfer reuse)');
  }
  return {
    origin: url.origin,
    entryUrl: url.toString(),
    template: String(spec.template ?? 'public-readonly-transfer'),
    goal: String(spec.goal ?? 'readonly-lookup'),
    fields: spec.fields,
    intermediateClicks: spec.intermediateClicks,
    queries: spec.queries,
  };
}

export function attachReadOnlyNetworkGuard(page, allowedOrigin) {
  const blockedRequests = [];
  const handler = async route => {
    const req = route.request();
    const method = req.method().toUpperCase();
    const reqUrl = new URL(req.url());
    const allowedScheme = reqUrl.protocol === 'data:' || reqUrl.protocol === 'blob:';
    if (!allowedScheme && (!isReadOnlyHttpMethod(method) || reqUrl.origin !== allowedOrigin)) {
      blockedRequests.push({
        method,
        url: req.url(),
        resourceType: req.resourceType(),
      });
      await route.abort('blockedbyclient');
      return;
    }
    await route.continue();
  };
  return {
    blockedRequests,
    install: () => page.route('**/*', handler),
  };
}

export async function startReadOnlyReferenceMirrorServer() {
  let postHits = 0;
  let visits = 0;
  const ARTICLES = [
    { slug: 'webgpu-limits', topic: 'WebGPU Adapter Limits', keyword: 'adapter', section: 'Specs' },
    { slug: 'cdp-targets', topic: 'CDP Target Attachment', keyword: 'target', section: 'Protocols' },
    { slug: 'aria-combobox', topic: 'ARIA Combobox Roles', keyword: 'combobox', section: 'Accessibility' },
  ];
  const server = createServer((req, res) => {
    const url = new URL(req.url, 'http://127.0.0.1');
    if (req.method !== 'GET' && req.method !== 'HEAD') {
      postHits += 1;
      res.writeHead(405, { 'content-type': 'text/plain' });
      res.end('Write method forbidden');
      return;
    }
    visits += 1;
    if (url.pathname === '/docs') {
      const q = (url.searchParams.get('q') ?? '').trim().toLowerCase();
      const sec = url.searchParams.get('section') ?? '';
      const matched = ARTICLES.find(a => (!sec || a.section === sec) && q && a.keyword.includes(q));
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html><html><head><meta charset="utf-8"><title>Reference Index</title></head><body>
        <h1>Public Reference Index</h1>
        <p>Trending today (#${visits}): <a href="/docs?q=adapter&section=Specs">Daily highlight ${visits * 7}</a></p>
        <label for="doc-q">Search reference</label>
        <input id="doc-q" type="search" value="${q}">
        <label for="doc-sec">Reference section</label>
        <select id="doc-sec">
          <option value="">All</option>
          <option value="Specs" ${sec === 'Specs' ? 'selected' : ''}>Specs</option>
          <option value="Protocols" ${sec === 'Protocols' ? 'selected' : ''}>Protocols</option>
          <option value="Accessibility" ${sec === 'Accessibility' ? 'selected' : ''}>Accessibility</option>
        </select>
        <button id="run-search" type="button" onclick="location.href='/docs?q='+encodeURIComponent(document.getElementById('doc-q').value)+'&section='+encodeURIComponent(document.getElementById('doc-sec').value)">Find topic</button>
        ${matched ? `<article><h2 id="topic-heading">${matched.topic}</h2></article>` : ''}
        <form method="POST" action="/newsletter"><button type="submit">Subscribe</button></form>
      </body></html>`);
      return;
    }
    res.writeHead(404);
    res.end('Not found');
  });
  await new Promise((r, j) => {
    server.once('error', j);
    server.listen(0, '127.0.0.1', r);
  });
  const origin = `http://127.0.0.1:${server.address().port}`;
  return {
    origin,
    getServerWriteHits: () => postHits,
    close: () => new Promise(r => server.close(r)),
  };
}

async function runCli() {
  const args = process.argv.slice(2);
  if (args.includes('--help')) {
    console.log('Usage: node browser/workspace/qualification/public-readonly-transfer.js --out .tmp/DIR [--spec spec.json] [--controlled] [--max-calls 20] [--max-cost-usd 1]');
    return;
  }
  const getFlag = name => {
    const i = args.indexOf(name);
    return i >= 0 ? args[i + 1] : null;
  };
  const controlled = args.includes('--controlled');
  const outRel = getFlag('--out');
  if (!outRel) throw Error('--out is required');
  if (!controlled && (!getFlag('--spec') || !getFlag('--max-calls') || !getFlag('--max-cost-usd'))) {
    throw Error('Live public read-only runs require --spec, --max-calls, and --max-cost-usd');
  }
  const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
  const out = resolve(root, outRel);
  if (existsSync(join(out, 'summary.json'))) throw Error(`Refusing to overwrite ${out}`);
  mkdirSync(out, { recursive: true, mode: 0o700 });

  const mirror = controlled && !getFlag('--spec') ? await startReadOnlyReferenceMirrorServer() : null;
  try {
    const rawSpec = getFlag('--spec')
      ? JSON.parse(readFileSync(resolve(root, getFlag('--spec')), 'utf8'))
      : {
          entryUrl: `${mirror.origin}/docs`,
          template: 'public-reference-index',
          goal: 'topic-heading',
          fields: [
            { slot: 'query', kind: 'fill', locator: ['textbox', 'Search reference', 'input', 'search'] },
            { slot: 'section', kind: 'select', locator: ['combobox', 'Reference section', 'select', ''] },
          ],
          intermediateClicks: [['button', 'Find topic', 'button', '']],
          queries: [
            { inputs: { query: 'adapter', section: 'Specs' }, expectedText: 'WebGPU Adapter Limits' },
            { inputs: { query: 'target', section: 'Protocols' }, expectedText: 'CDP Target Attachment' },
            { inputs: { query: 'combobox', section: 'Accessibility' }, expectedText: 'ARIA Combobox Roles' },
          ],
        };
    const validated = validateReadOnlyTransferSpec(rawSpec);
    const serviceConfig = loadServiceConfig(undefined, { browserChannel: 'chrome', headless: true });
    const clientConfig = JSON.parse(readFileSync(join(root, 'config/fawn-workspace-client.json'), 'utf8'));
    const storeDir = join(out, 'workflows');
    mkdirSync(storeDir, { recursive: true, mode: 0o700 });
    const rows = [];

    for (let i = 0; i < validated.queries.length; i += 1) {
      const q = validated.queries[i];
      const mode = i === 0 ? 'learn' : 'reuse';
      const wsId = `ro-${i + 1}`;
      const runDir = join(out, wsId);
      mkdirSync(runDir, { recursive: true, mode: 0o700 });
      let adapter;
      let service;
      let invStore;
      try {
        service = await WorkspaceService.open({
          stateDir: join(runDir, 'service'),
          config: serviceConfig,
          createAdapter: opts => { adapter = createPlaywrightAdapter(opts); return adapter; },
        });
        const admin = { workspaceId: wsId, ...await service.createWorkspace({ workspaceId: wsId, scopedReads: true }) };
        await service.grantAuthorization({
          ...admin,
          grantId: 'ro',
          ownerId: 'agent',
          intents: ['readonly-transfer'],
          actionKinds: ['navigate', 'fill', 'select', 'click'],
          targetOrigins: [validated.origin],
          pageIds: ['main'],
          readRegions: null,
        });
        const owner = { workspaceId: wsId, ...await service.assignOwner({ ...admin, ownerId: 'agent' }) };
        const obs0 = await service.observe(owner);
        await (await service.submitAction({
          ...owner,
          actionId: `nav-${wsId}`,
          authorizedIntent: 'readonly-transfer',
          kind: 'navigate',
          target: { url: validated.entryUrl },
          observationVersion: obs0.observationVersion,
        })).result;

        const page = adapter._page(wsId, 'main');
        const netGuard = attachReadOnlyNetworkGuard(page, validated.origin);
        await netGuard.install();

        invStore = new InvestigationStore(join(runDir, 'client'));
        invStore.create({ id: wsId, goal: `Lookup ${q.expectedText}` });
        const wfStore = new WorkflowStore({
          directory: storeDir,
          ownerScope: 'readonly-transfer',
          limits: { maxNodes: 64, maxEdges: 64, maxSteps: 16 },
          maxBytes: 2_000_000,
          lockWaitMs: 1000,
          lockRetryMs: 10,
        });

        const planner = controlled
          ? {
              identity: { model: 'controlled-readonly' },
              next: async ({ observation, inputs }) => {
                const els = observation.elements;
                const s = els.find(e => e.label === 'Search reference');
                const sec = els.find(e => e.label === 'Reference section');
                const btn = els.find(e => e.label === 'Find topic');
                if (observation.text.includes(q.expectedText)) return { tool: 'finish', input: { summary: 'Found' } };
                if (s && s.value !== inputs.query) return { tool: 'act', input: { kind: 'fill', handle: s.handle, slot: 'query' } };
                if (sec && sec.value !== inputs.section) return { tool: 'act', input: { kind: 'select', handle: sec.handle, slot: 'section' } };
                if (btn) return { tool: 'act', input: { kind: 'click', handle: btn.handle } };
                return { tool: 'ask_human', input: { reason: 'Missing control' } };
              },
            }
          : createCliPlanner({
              executable: process.env.FAWN_CLAUDE_BIN ?? 'claude',
              model: 'sonnet',
              directory: join(runDir, 'planner'),
              config: { ...clientConfig, cliMaxBudgetUsd: 0.15 },
              system: WORKFLOW_SYSTEM,
              tools: WORKFLOW_TOOLS,
            });

        const res = await wfStore.run({
          mode,
          goal: validated.goal,
          accountScope: 'public-readonly',
          template: validated.template,
          verifyAccount: async () => (new URL(page.url()).origin === validated.origin ? 'public-readonly' : null),
          verificationId: `ro-v1-${i}`,
          taskSourceHashes: { runner: 'a'.repeat(64) },
          disclosurePolicy: {
            origin: validated.origin,
            fields: validated.fields,
            intermediateClicks: validated.intermediateClicks,
          },
          learnRepairs: false,
          reconvergeMidRepair: false,
          service,
          owner,
          pageId: 'main',
          intent: 'readonly-transfer',
          inputs: q.inputs,
          task: `Look up "${q.expectedText}" in read-only mode without submitting any form.`,
          planner,
          config: { ...clientConfig, maxTurns: 10 },
          store: invStore,
          projectId: wsId,
          verifyReview: async () => ({
            completed: (await page.content()).includes(q.expectedText) && netGuard.blockedRequests.length === 0,
          }),
          signal: new AbortController().signal,
        });
        rows.push({
          queryIndex: i + 1,
          mode,
          completed: res.completed,
          modelCalls: res.modelCalls,
          cacheSteps: res.cacheSteps,
          blockedNonReadOnlyRequests: netGuard.blockedRequests.length,
        });
      } finally {
        invStore?.close();
        await service?.close();
      }
    }

    const summary = {
      schema: 'fawn.public-readonly-transfer/v1',
      controlled,
      origin: validated.origin,
      serverWriteHits: mirror ? mirror.getServerWriteHits() : 0,
      allCompleted: rows.every(r => r.completed),
      zeroBlockedWrites: rows.every(r => r.blockedNonReadOnlyRequests === 0),
      transferReuseZeroCalls: rows.slice(1).every(r => r.modelCalls === 0 && r.cacheSteps > 0),
      rows,
    };
    writeFileSync(join(out, 'summary.json'), `${JSON.stringify(summary, null, 2)}\n`);
    console.log(JSON.stringify(summary));
    if (!summary.allCompleted || !summary.zeroBlockedWrites || !summary.transferReuseZeroCalls) {
      process.exitCode = 1;
    }
  } finally {
    await mirror?.close();
  }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  await runCli();
}
