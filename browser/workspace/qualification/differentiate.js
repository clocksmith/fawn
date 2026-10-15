#!/usr/bin/env node
// Fawn workspace differentiation runner.
//
// Runs the frozen scenarios of one suite in workspaceComparison of
// config/browser-product-comparison-policy.json (differentiation-suites.js names each suite's
// application side). The treatment agent is a client of the workspace
// service; the control agent uses Playwright agent tooling. Both run the same policy and think
// time against a fresh workbench site per run, interleaved per repetition. The workbench's own
// orders, cart and mutation log judge every run; the frozen requirements decide each area.
//
// Usage: node qualification/differentiate.js [--suite differentiation|revalidatedDifferentiation]
//   [--out DIR] [--scenarios id,id] [--repetitions n] [--scripted-comparison comparison.json]
//   [--prior-suites differentiation.json,...]

import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadServiceConfig } from '../src/config.js';
import { Checkpoint } from './agents/checkpoint.js';
import { FawnAgent } from './agents/fawn-agent.js';
import { PlaywrightAgent } from './agents/playwright-agent.js';
import { SUITES } from './differentiation-suites.js';
import { REPO_ROOT, inspectBrowserProcesses, resultIdentity, waitForProfileRelease } from './identity.js';
import { startHost } from './service-client.js';

const POLICY_PATH = 'config/browser-product-comparison-policy.json';
const DEFAULT_OUT_ROOT = 'bench/out/fawn-workspace-differentiation';
const TOOL_HOST = fileURLToPath(new URL('./agents/playwright-tool-host.js', import.meta.url));
const DIFFERENTIATION_SCHEMA_VERSION = 1;
const ARMS = Object.freeze(['treatment', 'control']);
const WORKSPACE_ID = 'ws-differentiation';
const AGENT_ID = 'agent';
const HUMAN_ID = 'human-operator';
const AGENT_ACTION_KINDS = Object.freeze(['navigate', 'click', 'fill', 'submit', 'download']);
// Perturbation timing shared by both arms: how long the human keeps control after typing, and
// how long a killed agent takes to come back.
const HUMAN_HOLD_FACTOR = 2;
const AGENT_RESTART_DELAY_MS = 1500;
const POLL_MS = 25;
const WORKSPACE_ROOT = fileURLToPath(new URL('../', import.meta.url));
// Agent-side code each arm needs beyond the shared policy and the application profile.
const HARNESS_FILES = Object.freeze({
  treatment: ['qualification/agents/fawn-agent.js'],
  control: ['qualification/agents/playwright-agent.js', 'qualification/agents/playwright-tool-host.js'],
});

function codeLines(relativePath) {
  return readFileSync(join(WORKSPACE_ROOT, relativePath), 'utf8').split('\n').map((line) => line.trim())
    .filter((line) => line && !line.startsWith('//') && !line.startsWith('*') && !line.startsWith('/*')).length;
}

function parseArgs(argv) {
  const options = {};
  for (let index = 0; index < argv.length; index += 2) options[argv[index].replace(/^--/, '')] = argv[index + 1];
  return {
    out: options.out ?? null,
    scenarios: options.scenarios ? options.scenarios.split(',') : null,
    repetitions: options.repetitions ? Number(options.repetitions) : null,
    scriptedComparison: options['scripted-comparison'] ?? null,
    suite: options.suite ?? 'differentiation',
    priorSuites: options['prior-suites'] ? options['prior-suites'].split(',') : [],
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
  let takeovers = 0;
  const waiters = [];
  return {
    hold() {
      held = true;
      takeovers += 1;
    },
    takeovers: () => takeovers,
    release() {
      held = false;
      for (const resolve of waiters.splice(0)) resolve();
    },
    whileHeld: () => (held ? new Promise((resolve) => waiters.push(resolve)) : Promise.resolve()),
  };
}

class Arm {
  constructor({ name, site, scenario, definition }) {
    this.name = name;
    this.site = site;
    this.scenario = scenario;
    this.definition = definition;
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
        grantId: 'agent-task',
        ownerId: AGENT_ID,
        intents: [this.definition.intent],
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
      return new FawnAgent({ host: this.host, identity: { current: () => this.owner }, checkpoint, thinkTimeMs, task, profile: this.definition.profiles.treatment });
    }
    return new PlaywrightAgent({
      host: this.host,
      pause: this.pause,
      checkpoint,
      thinkTimeMs,
      task,
      profile: this.definition.profiles.control,
      ...this.definition.controlOptions,
    });
  }

  /** A human takes the browser, types an address, keeps control for a while, and hands back. */
  async humanTakesControl(window, holdMs) {
    window.start = new Date().toISOString();
    if (this.name === 'treatment') await this.assign(HUMAN_ID);
    else this.pause.hold();
    const before = this.site.state.mutations.filter((entry) => entry.kind === 'human-fill').length;
    await fetch(`${this.site.origin}/control/human?fill=${encodeURIComponent(this.definition.humanFill)}`);
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

async function runArm(name, scenario, repetition, suite, definition) {
  const site = await definition.startSite();
  const application = definition.applications[scenario.application ?? definition.defaultApplication];
  const flags = { ...application, churn: scenario.area === 'incremental-observation' };
  await fetch(`${site.origin}/control/setup?${new URLSearchParams(Object.entries(flags).map(([key, value]) => [key, value ? '1' : '0']))}`);
  const arm = new Arm({ name, site, scenario, definition });
  await arm.start();
  const id = `${scenario.id}-${name}-${repetition}`;
  const task = { id, intent: definition.intent, origin: site.origin, ...definition.task(site, id) };
  const humanWindow = {};
  let humanSequence = null;
  const startHumanControl = () => {
    humanSequence ??= arm.humanTakesControl(humanWindow, suite.thinkTimeMs * HUMAN_HOLD_FACTOR);
  };
  const hooks = { decision: (stage, info) => definition.perturb({ site, scenario, stage, info, startHumanControl }) };
  const started = performance.now();
  let incarnation = 1;
  let agent = arm.createAgent(task, suite.thinkTimeMs, incarnation);
  let observationBytes = 0;
  const settle = (promise) => promise.then((result) => ({ result, error: null }), (error) => ({ result: null, error: { code: error.code ?? null, message: error.message } }));
  let outcome;
  if (scenario.area === 'interruption-recovery') {
    const held = site.hold(definition.holdTarget);
    const running = settle(definition.policy(agent, task, hooks));
    const first = await Promise.race([held.reached.then(() => 'held'), running.then(() => 'finished')]);
    if (first === 'held') {
      agent.kill();
      held.release();
      await running;
      observationBytes += agent.observationBytes;
      await sleep(AGENT_RESTART_DELAY_MS);
      incarnation += 1;
      agent = arm.createAgent(task, suite.thinkTimeMs, incarnation);
      outcome = await settle(definition.policy(agent, task, hooks));
    } else {
      outcome = await running;
    }
  } else {
    outcome = await settle(definition.policy(agent, task, hooks));
  }
  await humanSequence;
  observationBytes += agent.observationBytes;
  const latencyMs = performance.now() - started;
  arm.inspect();
  const verdict = definition.judge(site, task, scenario, outcome, humanWindow);
  const evidence = { requests: site.state.requests.length, ...definition.evidence(site) };
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
  humanReviewsPlusDuplicateEffects: (rows) => sum(rows, 'humanReviews') + sum(rows, 'duplicateEffects'),
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
function regressions(rows, scenarios, latencyRule, duplicateField) {
  const found = [];
  for (const scenario of scenarios) {
    const treatment = rows.filter((row) => row.scenarioId === scenario.id && row.arm === 'treatment');
    const control = rows.filter((row) => row.scenarioId === scenario.id && row.arm === 'control');
    if (treatment.length === 0) continue;
    const successes = (subset) => subset.filter((row) => row.taskSuccess).length;
    if (successes(treatment) < successes(control)) found.push({ scenario: scenario.id, measure: 'taskSuccess' });
    for (const measure of ['incorrectActions', 'humanReviews', duplicateField, 'mutationsDuringHumanControl']) {
      if (sum(treatment, measure) > sum(control, measure)) found.push({ scenario: scenario.id, measure });
    }
    const latency = { treatment: median(treatment.map((row) => row.latencyMs)), control: median(control.map((row) => row.latencyMs)) };
    if (latency.treatment > latency.control * latencyRule.regressionRatio && latency.treatment - latency.control > latencyRule.minimumDeltaMs) {
      found.push({ scenario: scenario.id, measure: 'completeTaskLatencyMs', ...latency });
    }
  }
  return found;
}

function scenarioSummary(rows, scenario, duplicateField) {
  const pick = (arm) => rows.filter((row) => row.scenarioId === scenario.id && row.arm === arm);
  return Object.fromEntries(ARMS.map((arm) => {
    const subset = pick(arm);
    return [arm, {
      successes: subset.filter((row) => row.taskSuccess).length,
      repetitions: subset.length,
      incorrectActions: sum(subset, 'incorrectActions'),
      constraintViolatingMutations: sum(subset, 'constraintViolatingMutations'),
      humanReviews: sum(subset, 'humanReviews'),
      [duplicateField]: sum(subset, duplicateField),
      mutationsDuringHumanControl: sum(subset, 'mutationsDuringHumanControl'),
      medianObservationBytes: median(subset.map((row) => row.observationBytes)),
      medianLatencyMs: median(subset.map((row) => row.latencyMs)),
    }];
  }));
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const comparison = JSON.parse(readFileSync(join(REPO_ROOT, POLICY_PATH), 'utf8')).workspaceComparison;
  const suite = comparison[options.suite];
  const definition = SUITES[options.suite];
  if (!suite || !definition) throw new Error(`unknown suite ${options.suite}`);
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
        const run = await runArm(name, scenario, repetition, suite, definition);
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
  const suiteRegressions = regressions(rows, scenarios, comparison.criteria.latency, definition.duplicateField);
  let scriptedRegressions = null;
  if (options.scriptedComparison) {
    const scripted = JSON.parse(readFileSync(options.scriptedComparison, 'utf8'));
    scriptedRegressions = scripted.evaluation.flatMap((entry) => entry.findings.filter((finding) => finding.direction === 'regression')
      .map((finding) => ({ scenario: entry.scenarioId, measure: finding.measure })));
  }
  // Earlier frozen suites named by the rule must have run in full without regression.
  const priorRegressions = options.priorSuites.map((path) => {
    const prior = JSON.parse(readFileSync(path, 'utf8'));
    return { path, suiteVersion: prior.suiteVersion, full: prior.scope === 'full', regressions: prior.productAdvantage.suiteRegressions };
  });
  const priorsClean = priorRegressions.every((prior) => prior.full && prior.regressions.length === 0);
  const rule = suite.productAdvantageRule;
  const priorsRequired = rule.noMaterialRegressionIn.length - 2;
  const areasMet = requirements.filter((requirement) => requirement.status === 'met').map((requirement) => requirement.area);
  const isFull = !options.scenarios && !options.repetitions;
  const productAdvantage = {
    areasMet,
    suiteRegressions,
    scriptedRegressions,
    priorRegressions,
    established: isFull && areasMet.length >= rule.minimumAreasMet && suiteRegressions.length === 0
      && Array.isArray(scriptedRegressions) && scriptedRegressions.length === 0
      && priorRegressions.length >= priorsRequired && priorsClean,
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
    harnessLines: Object.fromEntries(Object.entries(HARNESS_FILES).map(([arm, files]) => [arm,
      Object.fromEntries(files.map((file) => [file, codeLines(file)]))])),
    perturbationTiming: { thinkTimeMs: suite.thinkTimeMs, humanHoldMs: suite.thinkTimeMs * HUMAN_HOLD_FACTOR, agentRestartDelayMs: AGENT_RESTART_DELAY_MS },
    suite: options.suite,
    summaries: Object.fromEntries(scenarios.map((scenario) => [scenario.id, scenarioSummary(rows, scenario, definition.duplicateField)])),
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
