#!/usr/bin/env node
// Fawn workspace differentiation runner.
//
// Runs the frozen scenarios in workspaceComparison.differentiation of
// config/browser-product-comparison-policy.json. The treatment agent is a client of the workspace
// service; the control agent uses Playwright agent tooling. Both run the same policy and think
// time against a fresh workbench site per run, interleaved per repetition. The workbench's own
// orders, cart and mutation log judge every run; the frozen requirements decide each area.
//
// Usage: node qualification/differentiate.js [--out DIR] [--scenarios id,id] [--repetitions n]
//   [--scripted-comparison path/to/comparison.json]

import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadServiceConfig } from '../src/config.js';
import { Checkpoint } from './agents/checkpoint.js';
import { FawnAgent } from './agents/fawn-agent.js';
import { PlaywrightAgent } from './agents/playwright-agent.js';
import { cheapestValid, runProcurement } from './agents/procurement-policy.js';
import { REPO_ROOT, inspectBrowserProcesses, resultIdentity, waitForProfileRelease } from './identity.js';
import { startHost } from './service-client.js';
import { startWorkbenchSite } from './workbench-site.js';

const POLICY_PATH = 'config/browser-product-comparison-policy.json';
const DEFAULT_OUT_ROOT = 'bench/out/fawn-workspace-differentiation';
const TOOL_HOST = fileURLToPath(new URL('./agents/playwright-tool-host.js', import.meta.url));
const DIFFERENTIATION_SCHEMA_VERSION = 1;
const ARMS = Object.freeze(['treatment', 'control']);
const WORKSPACE_ID = 'ws-differentiation';
const AGENT_ID = 'agent';
const HUMAN_ID = 'human-operator';
const INTENT = 'procure:one-item';
const AGENT_ACTION_KINDS = Object.freeze(['navigate', 'click', 'fill', 'submit', 'download']);
const BUDGET_CENTS = 1000;
const TASK_ADDRESS = '1 Task Street';
const HUMAN_ADDRESS = '9 Human Road';
const RAISED_PRICE_CENTS = 1500;
// Perturbation timing shared by both arms: how long the human keeps control after typing, and
// how long a killed agent takes to come back.
const HUMAN_HOLD_FACTOR = 2;
const AGENT_RESTART_DELAY_MS = 1500;
const POLL_MS = 25;

const APPLICATIONS = Object.freeze({
  'order-list-and-idempotency-reference': { orderList: true, idempotency: true, transientConfirmation: false },
  'no-order-list-no-idempotency-and-a-confirmation-that-redirects-away': { orderList: false, idempotency: false, transientConfirmation: true },
});
const DEFAULT_APPLICATION = 'order-list-and-idempotency-reference';

function parseArgs(argv) {
  const options = {};
  for (let index = 0; index < argv.length; index += 2) options[argv[index].replace(/^--/, '')] = argv[index + 1];
  return {
    out: options.out ?? null,
    scenarios: options.scenarios ? options.scenarios.split(',') : null,
    repetitions: options.repetitions ? Number(options.repetitions) : null,
    scriptedComparison: options['scripted-comparison'] ?? null,
  };
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function median(values) {
  const sorted = [...values].sort((a, b) => a - b);
  if (sorted.length === 0) return null;
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
}

async function until(predicate, limitMs, description) {
  const deadline = Date.now() + limitMs;
  while (!predicate()) {
    if (Date.now() > deadline) throw new Error(`timed out waiting for ${description}`);
    await sleep(POLL_MS);
  }
}

/** Shared by the control's tools: every call waits while a human holds the browser. */
function createPause() {
  let held = false;
  let waiters = [];
  return {
    hold() {
      held = true;
    },
    release() {
      held = false;
      for (const resolve of waiters.splice(0)) resolve();
    },
    whileHeld: () => (held ? new Promise((resolve) => waiters.push(resolve)) : Promise.resolve()),
  };
}

class Arm {
  constructor({ name, site, scenario }) {
    this.name = name;
    this.site = site;
    this.scenario = scenario;
    this.root = mkdtempSync(join(tmpdir(), `fawn-differentiation-${name}-`));
    this.stateDir = join(this.root, 'state');
    this.checkpointPath = join(this.root, 'checkpoint.json');
    this.inspections = [];
    this.pause = createPause();
    this.owner = null;
  }

  async start() {
    if (this.name === 'treatment') {
      this.host = await startHost({ stateDir: this.stateDir, adapter: 'playwright' });
      const { adminCredential } = await this.host.call('createWorkspace', { workspaceId: WORKSPACE_ID });
      this.adminCredential = adminCredential;
      await this.host.call('grantAuthorization', {
        workspaceId: WORKSPACE_ID,
        adminCredential,
        grantId: 'agent-procurement',
        ownerId: AGENT_ID,
        intents: [INTENT],
        actionKinds: AGENT_ACTION_KINDS,
        targetOrigins: [this.site.origin],
      });
      await this.assign(AGENT_ID);
    } else {
      this.host = await startHost({ stateDir: this.stateDir, actionTimeoutMs: loadServiceConfig().dispatch.actionTimeoutMs, headless: true }, TOOL_HOST);
    }
  }

  async assign(ownerId) {
    const assigned = await this.host.call('assignOwner', { workspaceId: WORKSPACE_ID, adminCredential: this.adminCredential, ownerId });
    if (ownerId === AGENT_ID) {
      this.owner = { workspaceId: WORKSPACE_ID, ownerId, ownerGeneration: assigned.ownerGeneration, ownerCredential: assigned.ownerCredential };
    }
    return assigned;
  }

  createAgent(task, thinkTimeMs, incarnation) {
    const checkpoint = new Checkpoint(this.checkpointPath);
    checkpoint.update({ incarnation });
    if (this.name === 'treatment') {
      return new FawnAgent({ host: this.host, identity: { current: () => this.owner }, checkpoint, thinkTimeMs, task });
    }
    return new PlaywrightAgent({ host: this.host, pause: this.pause, checkpoint, thinkTimeMs, task });
  }

  /** A human takes the browser, types an address, keeps control for a while, and hands back. */
  async humanTakesControl(window, holdMs) {
    window.start = new Date().toISOString();
    if (this.name === 'treatment') await this.assign(HUMAN_ID);
    else this.pause.hold();
    const before = this.site.state.mutations.filter((entry) => entry.kind === 'human-fill').length;
    await fetch(`${this.site.origin}/control/human?fill=${encodeURIComponent(HUMAN_ADDRESS)}`);
    await until(() => this.site.state.mutations.filter((entry) => entry.kind === 'human-fill').length > before, 10000, 'the human to type');
    await sleep(holdMs);
    window.end = new Date().toISOString();
    if (this.name === 'treatment') await this.assign(AGENT_ID);
    else this.pause.release();
  }

  inspect() {
    this.inspections.push(inspectBrowserProcesses(this.host.pid, this.stateDir));
  }

  async close() {
    await this.host.request('close', {}).catch(() => {});
    await this.host.exited;
    await waitForProfileRelease(this.stateDir);
    rmSync(this.root, { recursive: true, force: true });
  }
}

/** Judge one run from the workbench's own state. */
function judge(site, task, scenario, outcome, humanWindow) {
  const { orders, mutations } = site.state;
  const order = orders[0] ?? null;
  const expectedAddress = scenario.area === 'human-handoff' ? HUMAN_ADDRESS : TASK_ADDRESS;
  const valid = order ? cheapestValid(order.catalogAtOrder, task.budget) : null;
  const correctItem = Boolean(order && order.lines.length === 1 && valid && order.lines[0].slug === valid.slug
    && order.lines[0].priceAtOrder <= task.budget && order.lines[0].stockAtOrder > 0);
  const addressCorrect = order?.address === expectedAddress;
  const invoiceCorrect = Boolean(order && outcome.result?.invoiceSha256 === site.invoiceSha256(order));
  const duplicateOrders = Math.max(0, orders.length - 1);
  const constraintViolatingMutations = mutations.filter((entry) => entry.kind === 'cart-add'
    && (entry.price > task.budget || entry.stock <= 0)).length;
  const inWindow = (entry) => humanWindow.start && entry.at >= humanWindow.start && entry.at <= humanWindow.end;
  const mutationsDuringHumanControl = mutations.filter((entry) => ['cart-add', 'cart-remove', 'order'].includes(entry.kind) && inWindow(entry)).length;
  const humanReviews = outcome.result?.status === 'human-review' ? 1 : 0;
  return {
    taskSuccess: outcome.result?.status === 'completed' && orders.length === 1 && correctItem && addressCorrect && invoiceCorrect,
    incorrectActions: constraintViolatingMutations + duplicateOrders + (order && !correctItem ? 1 : 0) + (order && !addressCorrect ? 1 : 0),
    constraintViolatingMutations,
    mutationsDuringHumanControl,
    humanReviews,
    duplicateOrders,
    details: {
      status: outcome.result?.status ?? null,
      error: outcome.error,
      orders: orders.map((entry) => ({ id: entry.id, items: entry.lines.map((line) => line.slug), address: entry.address })),
      expectedItem: valid?.slug ?? null,
      addressCorrect,
      invoiceCorrect,
    },
  };
}

async function runArm(name, scenario, repetition, suite) {
  const site = await startWorkbenchSite();
  const application = APPLICATIONS[scenario.application ?? DEFAULT_APPLICATION];
  const flags = { ...application, churn: scenario.area === 'incremental-observation' };
  await fetch(`${site.origin}/control/setup?${new URLSearchParams(Object.entries(flags).map(([key, value]) => [key, value ? '1' : '0']))}`);
  const arm = new Arm({ name, site, scenario });
  await arm.start();
  const task = {
    id: `${scenario.id}-${name}-${repetition}`,
    intent: INTENT,
    origin: site.origin,
    budget: BUDGET_CENTS,
    address: TASK_ADDRESS,
    reference: `ref-${scenario.id}-${name}-${repetition}`,
    pageCount: site.pageCount,
  };
  const humanWindow = {};
  let humanSequence = null;
  const hooks = {
    async decision(stage, info) {
      if (scenario.area === 'changing-content' && stage === 'add' && info.decision === 1) {
        await fetch(`${site.origin}/control/price?item=${site.state.items.find((item) => item.name === info.item.name).slug}&price=${RAISED_PRICE_CENTS}`);
      }
      if (scenario.area === 'human-handoff' && stage === 'fill-address' && !humanSequence) {
        humanSequence = arm.humanTakesControl(humanWindow, suite.thinkTimeMs * HUMAN_HOLD_FACTOR);
      }
    },
  };
  const started = performance.now();
  let incarnation = 1;
  let agent = arm.createAgent(task, suite.thinkTimeMs, incarnation);
  let observationBytes = 0;
  const settle = (promise) => promise.then((result) => ({ result, error: null }), (error) => ({ result: null, error: { code: error.code ?? null, message: error.message } }));
  let outcome;
  if (scenario.area === 'interruption-recovery') {
    const held = site.hold({ method: 'POST', path: '/orders' });
    const running = settle(runProcurement(agent, task, hooks));
    const first = await Promise.race([held.reached.then(() => 'held'), running.then(() => 'finished')]);
    if (first === 'held') {
      agent.kill();
      held.release();
      await running;
      observationBytes += agent.observationBytes;
      await sleep(AGENT_RESTART_DELAY_MS);
      incarnation += 1;
      agent = arm.createAgent(task, suite.thinkTimeMs, incarnation);
      outcome = await settle(runProcurement(agent, task, hooks));
    } else {
      outcome = await running;
    }
  } else {
    outcome = await settle(runProcurement(agent, task, hooks));
  }
  await humanSequence;
  observationBytes += agent.observationBytes;
  const latencyMs = performance.now() - started;
  arm.inspect();
  const verdict = judge(site, task, scenario, outcome, humanWindow);
  const evidence = { requests: site.state.requests.length, mutations: site.state.mutations, orders: site.state.orders.map(({ catalogAtOrder, ...order }) => order) };
  await arm.close();
  await site.close();
  return {
    row: { scenarioId: scenario.id, arm: name, repetition, ...verdict, observationBytes, latencyMs, agentIncarnations: incarnation },
    inspections: arm.inspections,
    evidence,
  };
}

const sum = (rows, field) => rows.reduce((total, row) => total + row[field], 0);
const MEASURES = Object.freeze({
  humanReviewsPlusDuplicateOrders: (rows) => sum(rows, 'humanReviews') + sum(rows, 'duplicateOrders'),
});

function measureOf(rows, measure) {
  return MEASURES[measure] ? MEASURES[measure](rows) : sum(rows, measure);
}

function checkBar(rows, bar) {
  return Object.entries(bar).map(([field, expected]) => {
    if (field === 'taskSuccessEveryRepetition') return { field, expected, observed: rows.every((row) => row.taskSuccess), met: rows.every((row) => row.taskSuccess) === expected };
    const observed = sum(rows, field);
    return { field, expected, observed, met: observed === expected };
  });
}

function evaluateRequirement(requirement, rows) {
  const treatment = rows.filter((row) => row.scenarioId === requirement.scenario && row.arm === 'treatment');
  const control = rows.filter((row) => row.scenarioId === requirement.scenario && row.arm === 'control');
  if (treatment.length === 0) return { ...requirement, status: 'not-run' };
  const bar = checkBar(treatment, requirement.treatmentBar);
  const { measure } = requirement.materialImprovement;
  let improvement;
  if (requirement.materialImprovement.maximumTreatmentToControlRatio !== undefined) {
    const ratio = median(treatment.map((row) => row[measure])) / median(control.map((row) => row[measure]));
    improvement = { measure, ratio, maximum: requirement.materialImprovement.maximumTreatmentToControlRatio, met: ratio <= requirement.materialImprovement.maximumTreatmentToControlRatio };
  } else {
    const observed = { treatment: measureOf(treatment, measure), control: measureOf(control, measure) };
    improvement = { measure, ...observed, met: observed.treatment < observed.control };
  }
  const met = bar.every((entry) => entry.met) && improvement.met;
  return { area: requirement.area, scenario: requirement.scenario, status: met ? 'met' : 'not-met', bar, improvement };
}

/** A regression is any scenario where the treatment is materially worse on a decisive measure. */
function regressions(rows, scenarios, latencyRule) {
  const found = [];
  for (const scenario of scenarios) {
    const treatment = rows.filter((row) => row.scenarioId === scenario.id && row.arm === 'treatment');
    const control = rows.filter((row) => row.scenarioId === scenario.id && row.arm === 'control');
    if (treatment.length === 0) continue;
    const successes = (subset) => subset.filter((row) => row.taskSuccess).length;
    if (successes(treatment) < successes(control)) found.push({ scenario: scenario.id, measure: 'taskSuccess' });
    for (const measure of ['incorrectActions', 'humanReviews', 'duplicateOrders', 'mutationsDuringHumanControl']) {
      if (sum(treatment, measure) > sum(control, measure)) found.push({ scenario: scenario.id, measure });
    }
    const latency = { treatment: median(treatment.map((row) => row.latencyMs)), control: median(control.map((row) => row.latencyMs)) };
    if (latency.treatment > latency.control * latencyRule.regressionRatio && latency.treatment - latency.control > latencyRule.minimumDeltaMs) {
      found.push({ scenario: scenario.id, measure: 'completeTaskLatencyMs', ...latency });
    }
  }
  return found;
}

function scenarioSummary(rows, scenario) {
  const pick = (arm) => rows.filter((row) => row.scenarioId === scenario.id && row.arm === arm);
  return Object.fromEntries(ARMS.map((arm) => {
    const subset = pick(arm);
    return [arm, {
      successes: subset.filter((row) => row.taskSuccess).length,
      repetitions: subset.length,
      incorrectActions: sum(subset, 'incorrectActions'),
      constraintViolatingMutations: sum(subset, 'constraintViolatingMutations'),
      humanReviews: sum(subset, 'humanReviews'),
      duplicateOrders: sum(subset, 'duplicateOrders'),
      mutationsDuringHumanControl: sum(subset, 'mutationsDuringHumanControl'),
      medianObservationBytes: median(subset.map((row) => row.observationBytes)),
      medianLatencyMs: median(subset.map((row) => row.latencyMs)),
    }];
  }));
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const comparison = JSON.parse(readFileSync(join(REPO_ROOT, POLICY_PATH), 'utf8')).workspaceComparison;
  const suite = comparison.differentiation;
  const stamp = new Date().toISOString().replace(/[:.]/g, '').slice(0, 15);
  const outDir = options.out ?? join(REPO_ROOT, DEFAULT_OUT_ROOT, `${stamp}Z`);
  mkdirSync(outDir, { recursive: true });
  const scenarios = suite.scenarios.filter((scenario) => !options.scenarios || options.scenarios.includes(scenario.id));
  const rows = [];
  const inspections = { treatment: [], control: [] };
  const evidence = [];
  for (const scenario of scenarios) {
    const repetitions = options.repetitions ?? scenario.repetitions;
    for (let repetition = 1; repetition <= repetitions; repetition += 1) {
      for (const name of repetition % 2 ? ARMS : [...ARMS].reverse()) {
        const run = await runArm(name, scenario, repetition, suite);
        rows.push(run.row);
        inspections[name].push(...run.inspections);
        evidence.push({ scenarioId: scenario.id, arm: name, repetition, ...run.evidence });
        console.log(`ran: ${scenario.id} ${name} ${repetition}: ${run.row.taskSuccess ? 'success' : 'failure'}`
          + ` incorrect ${run.row.incorrectActions} reviews ${run.row.humanReviews} bytes ${run.row.observationBytes}`
          + ` ${Math.round(run.row.latencyMs)} ms${run.row.details.error ? ` (${run.row.details.error.message})` : ''}`);
      }
    }
  }
  const requirements = suite.improvementRequirements.map((requirement) => evaluateRequirement(requirement, rows));
  const suiteRegressions = regressions(rows, scenarios, comparison.criteria.latency);
  let scriptedRegressions = null;
  if (options.scriptedComparison) {
    const scripted = JSON.parse(readFileSync(options.scriptedComparison, 'utf8'));
    scriptedRegressions = scripted.evaluation.flatMap((entry) => entry.findings.filter((finding) => finding.direction === 'regression')
      .map((finding) => ({ scenario: entry.scenarioId, measure: finding.measure })));
  }
  const rule = suite.productAdvantageRule;
  const areasMet = requirements.filter((requirement) => requirement.status === 'met').map((requirement) => requirement.area);
  const isFull = !options.scenarios && !options.repetitions;
  const productAdvantage = {
    areasMet,
    suiteRegressions,
    scriptedRegressions,
    established: isFull && areasMet.length >= rule.minimumAreasMet && suiteRegressions.length === 0
      && Array.isArray(scriptedRegressions) && scriptedRegressions.length === 0,
    doesNotEstablish: rule.doesNotEstablish,
  };
  const executables = Object.fromEntries(ARMS.map((name) => [name, [...new Map(inspections[name].flatMap((inspection) => inspection.executables)
    .map((executable) => [executable.sha256, executable])).values()]]));
  const result = {
    schemaVersion: DIFFERENTIATION_SCHEMA_VERSION,
    suiteVersion: suite.suiteVersion,
    scope: isFull ? 'full' : 'subset',
    identity: resultIdentity(),
    browsers: executables,
    sameBrowserBinary: executables.treatment.length === 1 && executables.control.length === 1
      && executables.treatment[0].sha256 === executables.control[0].sha256,
    debuggingEndpoints: [...inspections.treatment, ...inspections.control]
      .reduce((total, inspection) => total + inspection.listeningTcpSockets + inspection.devToolsActivePortFiles, 0),
    perturbationTiming: { thinkTimeMs: suite.thinkTimeMs, humanHoldMs: suite.thinkTimeMs * HUMAN_HOLD_FACTOR, agentRestartDelayMs: AGENT_RESTART_DELAY_MS },
    summaries: Object.fromEntries(scenarios.map((scenario) => [scenario.id, scenarioSummary(rows, scenario)])),
    requirements,
    productAdvantage,
    rows,
    evidence,
  };
  writeFileSync(join(outDir, 'differentiation.json'), `${JSON.stringify(result, null, 2)}\n`);
  for (const requirement of requirements) {
    console.log(`${requirement.area}: ${requirement.status} ${JSON.stringify(requirement.improvement ?? {})}`);
  }
  console.log(`regressions: ${JSON.stringify(suiteRegressions)}; scripted: ${JSON.stringify(scriptedRegressions)}`);
  console.log(`product advantage established: ${productAdvantage.established}; same browser binary: ${result.sameBrowserBinary}; ${join(outDir, 'differentiation.json')}`);
  process.exit(0);
}

await main();
