#!/usr/bin/env node
// Balanced fresh launches, promoted from the retained 20261031 Windows harness.
import { spawnSync, execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmdirSync, writeFileSync, appendFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { PAGES } from './pages.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, '../../..');
const usage = `usage: doe-bench browser --config FILE --out DIR [--reps N] [--pages a,b] [--dry-run]

Run the selected pages through each configured browser arm in counterbalanced
fresh-launch order. --dry-run prints the schedule without launching or writing.
One page, one repetition and three arms require three launches. Partial schedules
remain diagnostic; complete positional balance needs a multiple of arms! repeats.
`;

function parseArgs(argv) {
  const options = {};
  for (let i = 0; i < argv.length; i += 1) {
    const key = argv[i];
    if (key === '--help' || key === '-h') return { help: true };
    if (key === '--dry-run') { options.dryRun = true; continue; }
    if (!['--config', '--out', '--reps', '--pages'].includes(key)) throw new Error(`Unknown option: ${key}`);
    if (!argv[i + 1] || argv[i + 1].startsWith('--')) throw new Error(`Missing value: ${key}`);
    options[key.slice(2)] = argv[++i];
  }
  if (!options.config || !options.out) throw new Error('--config and --out are required');
  return options;
}

function permutations(values) {
  if (values.length <= 1) return [values];
  return values.flatMap((value, i) => permutations([...values.slice(0, i), ...values.slice(i + 1)])
    .map(rest => [value, ...rest]));
}

const sleepSync = ms => Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms);
function windowsBuildActive() {
  const output = execFileSync('tasklist', ['/FO', 'CSV', '/NH'], { encoding: 'utf8' });
  return output.split('\n').map(line => line.split(',')[0]?.replace(/"/g, '').toLowerCase())
    .filter(name => /^(clang-cl|cl|ninja|siso|lld-link|link)\.exe$/.test(name));
}

function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.help) { console.log(usage); return; }
  const cfg = JSON.parse(readFileSync(options.config, 'utf8'));
  const reps = Number(options.reps ?? cfg.reps ?? 12);
  if (!Number.isSafeInteger(reps) || reps < 1) throw new Error('--reps must be a positive integer');
  const pages = options.pages ? options.pages.split(',') : cfg.pages;
  if (!Array.isArray(pages) || !pages.length || new Set(pages).size !== pages.length)
    throw new Error('Select a nonempty, unique page list');
  for (const page of pages) if (!Object.hasOwn(PAGES, page)) throw new Error(`Unknown page: ${page}`);
  const arms = cfg.arms;
  if (!Array.isArray(arms) || !arms.length || arms.length > 6 || new Set(arms.map(arm => arm.id)).size !== arms.length)
    throw new Error('Configuration needs 1..6 browser arms with unique identities');
  for (const arm of arms) {
    if (!/^[a-zA-Z0-9_-]+$/.test(arm.id) || typeof arm.browserPath !== 'string' || !arm.browserPath ||
        !Array.isArray(arm.args) || !arm.args.every(arg => typeof arg === 'string') || typeof arm.expectDoe !== 'boolean')
      throw new Error(`Invalid browser arm: ${JSON.stringify(arm)}`);
  }
  if (typeof cfg.lock !== 'string' || !cfg.lock) throw new Error('Configuration needs an explicit GPU timing lock');
  const out = resolve(options.out);
  const perms = permutations(arms.map(arm => arm.id));
  const schedule = [];
  for (let rep = 0; rep < reps; rep += 1) {
    for (const [pageIndex, page] of pages.entries()) {
      perms[(rep + pageIndex) % perms.length].forEach((arm, position) => schedule.push({ rep, page, arm, position }));
    }
  }
  const plan = { reps, pages, arms: arms.map(arm => arm.id), permutations: perms, schedule };
  if (options.dryRun) { console.log(JSON.stringify(plan, null, 2)); return; }
  const schedulePath = join(out, 'schedule.json');
  if (existsSync(schedulePath) && JSON.stringify(JSON.parse(readFileSync(schedulePath, 'utf8'))) !== JSON.stringify(plan))
    throw new Error(`Schedule changed: ${schedulePath}; use a new output directory`);
  mkdirSync(join(out, 'launches'), { recursive: true });
  const log = message => {
    const line = `${new Date().toISOString()} ${message}`;
    console.log(line); appendFileSync(join(out, 'orchestrate.log'), line + '\n');
  };
  writeFileSync(schedulePath, JSON.stringify(plan, null, 2) + '\n');
  let failed = false;
  for (const [index, row] of schedule.entries()) {
    const arm = arms.find(value => value.id === row.arm);
    const name = `${row.page}.${row.arm}.r${String(row.rep).padStart(2, '0')}`;
    const resultPath = join(out, 'launches', `${name}.json`);
    const spec = { repoRoot: ROOT, ...cfg.launch, ...cfg.pageOverrides?.[row.page],
      ...row, browserPath: arm.browserPath,
      args: arm.args, expectDoe: arm.expectDoe, keepPng: row.rep === 0 };
    if (existsSync(resultPath)) {
      const previous = JSON.parse(readFileSync(resultPath, 'utf8'));
      if (JSON.stringify(previous.spec) !== JSON.stringify(spec)) throw new Error(`Receipt configuration changed: ${resultPath}; use a new output directory`);
      failed ||= !previous.ok;
      continue;
    }
    const specPath = join(out, 'launches', `${name}.spec.json`);
    writeFileSync(specPath, JSON.stringify(spec));
    const command = [join(HERE, 'launch.mjs'), '--spec', specPath, '--out', resultPath];
    let result;
    if (process.platform === 'darwin' || process.platform === 'linux') {
      result = spawnSync('flock', [cfg.lock, process.execPath, ...command], { stdio: 'inherit', timeout: 300000 });
    } else if (process.platform === 'win32') {
      for (;;) {
        const busy = windowsBuildActive();
        if (busy.length) { log(`build active (${[...new Set(busy)].join(',')}); waiting`); sleepSync(1000); continue; }
        try { mkdirSync(cfg.lock); break; } catch (error) {
          if (error.code !== 'EEXIST') throw error;
          log('GPU timing lock held; waiting'); sleepSync(1000);
        }
      }
      try { result = spawnSync(process.execPath, command, { stdio: 'inherit', timeout: 300000 }); }
      finally { rmdirSync(cfg.lock); }
    } else throw new Error(`Unsupported timing-lock platform: ${process.platform}`);
    log(`[${index + 1}/${schedule.length}] ${name} exit=${result.status}${result.error ? ' ' + result.error : ''}`);
    failed ||= result.status !== 0 || !existsSync(resultPath) || !JSON.parse(readFileSync(resultPath, 'utf8')).ok;
    if (result.error || result.signal) throw new Error(`Launch did not finish normally: ${name}; remaining schedule stopped`);
  }
  log(failed ? 'schedule complete with failed launches' : 'schedule complete');
  if (failed) process.exitCode = 1;
}

try { main(); } catch (error) { console.error(error.message); process.exitCode = 1; }
