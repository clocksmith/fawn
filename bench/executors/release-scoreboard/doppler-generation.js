#!/usr/bin/env node
// Release scoreboard rows 1-3: unchanged Doppler text generation driven
// through its public compat API on one selected WebGPU provider. The model
// files, Doppler source, prompt, sampling and stopping are identical across
// providers; the observer records the WGSL and pipeline set each provider was
// given so the comparator can reject a row whose shaders differ.
//
// Modes:
//   decode-warm       load, warm up, then time decode of the declared tokens
//   prefill-warm      load, warm up, then time a long-prompt prefill
//   cold-first-token  process start through the first generated token, with
//                     per-phase attribution at the WebGPU API boundary
const harnessEvaluatedMs = performance.now();

import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

import {
  ApiObserver,
  OutputMismatchError,
  installGpuGlobals,
  loadProvider,
  loadScenario,
  repoPath,
  runExecutor,
  sha256,
  sha256File,
} from './common.js';

const USAGE = 'node bench/executors/release-scoreboard/doppler-generation.js '
  + '--provider doe|dawn --backend metal|vulkan|d3d12 --scenario <path> '
  + '--trace-meta <path> --trace-jsonl <path> --workload <id>';
const SCENARIO_KIND = 'release-scoreboard-doppler-generation';
const MODES = new Set(['decode-warm', 'prefill-warm', 'cold-first-token']);
const MODEL_ROOT_ENV = 'FAWN_SCOREBOARD_MODEL_ROOT';
const DOPPLER_ROOT_ENV = 'FAWN_SCOREBOARD_DOPPLER_ROOT';
const DEFAULT_MODEL_ROOT = 'bench/out/release-scoreboard/models';

function resolveDopplerRoot(scenario) {
  return repoPath(process.env[DOPPLER_ROOT_ENV]?.trim() || scenario.doppler.root);
}

function dopplerSourceIdentity(root, requireClean) {
  const commit = execFileSync('git', ['-C', root, 'rev-parse', 'HEAD'], { encoding: 'utf8' }).trim();
  const status = execFileSync('git', ['-C', root, 'status', '--porcelain', '--untracked-files=no'],
    { encoding: 'utf8' }).trim();
  if (requireClean && status !== '') {
    throw new Error('Doppler tracked source must be clean for a scoreboard row');
  }
  const version = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8')).version;
  return { dopplerCommit: commit, dopplerVersion: version, dopplerTrackedClean: status === '' };
}

// Cheap per-process custody: the manifest digest is frozen by the scenario
// and the shard bytes were hashed once by prepare_release_scoreboard_models.py.
function modelCustody(modelDir, model) {
  const manifestPath = join(modelDir, 'manifest.json');
  if (!existsSync(manifestPath)) {
    throw new Error(`model ${model.id} is not prepared under ${modelDir}; run `
      + 'bench/tools/prepare_release_scoreboard_models.py first');
  }
  const manifestSha256 = sha256File(manifestPath);
  if (manifestSha256 !== model.manifestSha256) {
    throw new Error(`model ${model.id} manifest digest ${manifestSha256} != declared ${model.manifestSha256}`);
  }
  const custodyPath = join(modelDir, 'custody.json');
  const custody = existsSync(custodyPath) ? JSON.parse(readFileSync(custodyPath, 'utf8')) : null;
  if (!custody || custody.manifestSha256 !== manifestSha256 || custody.allShardsVerified !== true) {
    throw new Error(`model ${model.id} has no verified custody receipt for this manifest`);
  }
  const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
  for (const shard of manifest.shards) {
    const size = statSync(join(modelDir, shard.filename)).size;
    if (size !== shard.size) {
      throw new Error(`model ${model.id} shard ${shard.filename} is ${size} bytes, manifest says ${shard.size}`);
    }
  }
  return {
    modelId: model.id,
    manifestSha256,
    shardCount: manifest.shards.length,
    totalBytes: manifest.totalSize,
    custodySha256: sha256File(custodyPath),
  };
}

function buildPrompt(prompt) {
  if (prompt.kind === 'text') return prompt.text;
  if (prompt.kind === 'repeat') {
    if (!Number.isInteger(prompt.count) || prompt.count < 1) throw new Error('repeat prompt needs count >= 1');
    return Array.from({ length: prompt.count }, (_, index) => prompt.text.replaceAll('{n}', String(index + 1)))
      .join(prompt.separator ?? '\n');
  }
  throw new Error(`unsupported prompt kind ${prompt.kind}`);
}

async function generate(model, prompt, options) {
  const tokens = [];
  const tokenTimesMs = [];
  let firstTokenAtMs = null;
  let text = '';
  const started = performance.now();
  for await (const chunk of model.generate(prompt, {
    ...options,
    onToken(id) {
      const now = performance.now();
      firstTokenAtMs ??= now;
      tokenTimesMs.push(now - started);
      tokens.push(id);
    },
  })) {
    text += chunk;
  }
  return { tokens, tokenTimesMs, firstTokenAtMs, text, completeMs: performance.now() - started, stats: model.advanced.getStats() };
}

// References are recorded per backend: different GPUs and shader compilers
// may legitimately round differently, while both providers on one host must
// agree with each other and with that host class's recorded tokens.
function checkReference(workload, backend, observed) {
  const reference = workload.references?.[backend] ?? null;
  if (!reference) {
    throw new OutputMismatchError(
      `workload ${workload.id} has no recorded ${backend} reference; observed tokens `
        + `${JSON.stringify(observed.tokenIds)} (record with bench/tools/record_release_scoreboard_reference.py)`,
      { observed },
    );
  }
  const expected = reference.tokenIds;
  if (reference.promptTokenCount !== observed.promptTokenCount) {
    throw new OutputMismatchError(
      `prompt token count ${observed.promptTokenCount} != recorded ${reference.promptTokenCount}`,
      { observed, reference },
    );
  }
  if (JSON.stringify(expected) !== JSON.stringify(observed.tokenIds)) {
    const index = expected.findIndex((id, position) => id !== observed.tokenIds[position]);
    throw new OutputMismatchError(
      `generated token ids differ from the recorded reference at position ${index}`,
      { observed, reference },
    );
  }
}

function phaseBreakdown(observer, windows) {
  const phase = (name) => observer.phases[name] ?? {};
  const pipelineMs = (bucket) => (bucket.pipelineSyncMs ?? 0) + (bucket.pipelineAsyncResolveMs ?? 0);
  const load = phase('model-load');
  const first = phase('first-generation');
  const loadApiMs = (load.writeBufferMs ?? 0) + (load.shaderModuleMs ?? 0) + pipelineMs(load);
  const firstApiMs = (first.shaderModuleMs ?? 0) + pipelineMs(first) + (first.writeBufferMs ?? 0);
  return {
    attribution: 'WebGPU API calls timed inside each harness window; Doppler interleaves '
      + 'file reads, uploads and pipeline creation, so phases are sums of observed calls',
    processStartToHarnessMs: windows.harnessEvaluatedMs,
    providerImportMs: windows.providerImportMs,
    dopplerImportMs: windows.dopplerImportMs,
    modelLoadWallMs: windows.modelLoadMs,
    loadingHostMs: windows.modelLoadMs - loadApiMs,
    uploadWriteBufferMs: (load.writeBufferMs ?? 0) + (first.writeBufferMs ?? 0),
    uploadWriteBufferBytes: (load.writeBufferBytes ?? 0) + (first.writeBufferBytes ?? 0),
    uploadMappedAtCreationBytes: (load.mappedAtCreationBytes ?? 0) + (first.mappedAtCreationBytes ?? 0),
    shaderModuleCreateMs: (load.shaderModuleMs ?? 0) + (first.shaderModuleMs ?? 0),
    shaderModuleCreateScope: 'WGSL parse and validation; Doe may also translate here',
    pipelineCreateMs: pipelineMs(load) + pipelineMs(first),
    pipelineCreateScope: 'translation to the native shading language plus native compilation; '
      + 'not separable at the WebGPU API boundary',
    firstGenerationWallMs: windows.firstTokenMs,
    firstExecutionMs: windows.firstTokenMs - firstApiMs,
    asyncPipelineCreation: observer.asyncPipelineSummary(),
    apiPhases: observer.phases,
  };
}

async function run(args, onTeardown) {
  const { scenario, workload, scenarioSha256 } = loadScenario(args.scenarioPath, SCENARIO_KIND, args.workloadId);
  if (!MODES.has(workload.mode)) throw new Error(`unsupported mode ${workload.mode}`);
  const cold = workload.mode === 'cold-first-token';
  const dopplerRoot = resolveDopplerRoot(scenario);
  const modelDir = join(repoPath(process.env[MODEL_ROOT_ENV]?.trim() || DEFAULT_MODEL_ROOT), workload.model.id);
  // No persistent pipeline cache is enabled on either provider; driver
  // shader caches and the OS file cache remain uncontrolled and declared.
  delete process.env.DOE_PIPELINE_CACHE_DIR;
  // Warm rows verify custody before work; the cold row verifies it after the
  // first token so the checks stay outside process-start-to-first-token.
  const custodyBefore = cold ? null : modelCustody(modelDir, workload.model);
  const sourceBefore = cold ? null : dopplerSourceIdentity(dopplerRoot, scenario.doppler.requireCleanTree);

  const observer = new ApiObserver();
  observer.enter('provider-import');
  const providerStarted = performance.now();
  const provider = await loadProvider(args.provider, args.backend);
  const gpu = observer.wrapGpu(provider.module.create(provider.contract.createOptions), provider.contract);
  installGpuGlobals(provider.module, gpu, provider.contract.createOptions);
  const providerImportMs = performance.now() - providerStarted;

  observer.enter('doppler-import');
  const dopplerStarted = performance.now();
  const api = await import(pathToFileURL(join(dopplerRoot, 'src/index.js')).href);
  const dopplerImportMs = performance.now() - dopplerStarted;

  observer.enter('model-load');
  const loadStarted = performance.now();
  const model = await api.load(
    { url: pathToFileURL(`${modelDir}/`).href },
    { cache: false, isolatedLoader: true, onProgress() {} },
  );
  const modelLoadMs = performance.now() - loadStarted;
  onTeardown(() => model.unload());

  const prompt = buildPrompt(workload.prompt);
  const options = {
    maxTokens: workload.maxTokens,
    temperature: 0,
    topK: 1,
    topP: 1,
    repetitionPenalty: 1,
    useChatTemplate: workload.useChatTemplate === true,
  };

  let timed;
  let warmups = [];
  let firstTokenProcessMs = null;
  if (cold) {
    observer.enter('first-generation');
    timed = await generate(model, prompt, options);
    firstTokenProcessMs = timed.firstTokenAtMs;
  } else {
    observer.enter('warmup');
    for (let index = 0; index < workload.warmupGenerations; index += 1) {
      const warm = await generate(model, prompt, options);
      warmups.push({ tokenIds: warm.tokens, completeMs: warm.completeMs });
    }
    observer.enter('timed');
    timed = await generate(model, prompt, options);
  }
  observer.enter('post-timing');

  const promptTokenCount = model.advanced.tokenizePrompt(prompt, {
    useChatTemplate: options.useChatTemplate,
  }).length;
  const custody = custodyBefore ?? modelCustody(modelDir, workload.model);
  const source = sourceBefore ?? dopplerSourceIdentity(dopplerRoot, scenario.doppler.requireCleanTree);
  const stats = timed.stats ?? {};
  if (timed.tokens.length !== workload.maxTokens || stats.stopReason !== 'max-tokens') {
    throw new OutputMismatchError(
      `expected ${workload.maxTokens} tokens stopped by max-tokens, got ${timed.tokens.length} (${stats.stopReason})`,
      { tokenIds: timed.tokens },
    );
  }
  for (const warm of warmups) {
    if (JSON.stringify(warm.tokenIds) !== JSON.stringify(timed.tokens)) {
      throw new OutputMismatchError('warmup and timed generations produced different tokens',
        { warmup: warm.tokenIds, timed: timed.tokens });
    }
  }
  const outputBytes = JSON.stringify(timed.tokens);
  const observed = { tokenIds: timed.tokens, promptTokenCount, text: timed.text };

  let timingMs;
  let timingSource;
  let unitWallMs;
  const metrics = {};
  if (workload.mode === 'decode-warm') {
    const decodeWindowMs = timed.tokenTimesMs.at(-1) - timed.tokenTimesMs[0];
    const decodedTokens = timed.tokens.length - 1;
    timingMs = decodeWindowMs / decodedTokens;
    timingSource = 'release-scoreboard-decode-ms-per-token';
    unitWallMs = timed.completeMs / decodedTokens;
    Object.assign(metrics, {
      decodeWindowMs, decodedTokens, decodeTokensPerSecond: (decodedTokens * 1000) / decodeWindowMs,
      firstTokenMs: timed.tokenTimesMs[0], completeGenerationMs: timed.completeMs,
      dopplerDecodeTimeMs: stats.decodeTimeMs ?? null, dopplerPrefillTimeMs: stats.prefillTimeMs ?? null,
    });
  } else if (workload.mode === 'prefill-warm') {
    timingMs = timed.tokenTimesMs[0];
    timingSource = 'release-scoreboard-prefill-first-token-ms';
    unitWallMs = timed.completeMs;
    Object.assign(metrics, {
      promptTokenCount, prefillToFirstTokenMs: timingMs,
      prefillTokensPerSecond: (promptTokenCount * 1000) / timingMs,
      dopplerPrefillTimeMs: stats.prefillTimeMs ?? null, dopplerTtftMs: stats.ttftMs ?? null,
    });
  } else {
    timingMs = firstTokenProcessMs;
    timingSource = 'release-scoreboard-cold-process-first-token-ms';
    unitWallMs = timed.firstTokenAtMs - timed.tokenTimesMs[0] + timed.completeMs;
    Object.assign(metrics, {
      processStartToFirstTokenMs: firstTokenProcessMs,
      phases: phaseBreakdown(observer, {
        harnessEvaluatedMs, providerImportMs, dopplerImportMs, modelLoadMs,
        firstTokenMs: timed.tokenTimesMs[0],
      }),
      dopplerLoadTiming: stats.loadTiming ?? null,
      dopplerPipelineLoadTiming: stats.pipelineLoadTiming ?? null,
    });
  }

  const executionPlan = stats.executionPlan?.primary ?? null;
  const fields = {
    timingMs,
    timingSource,
    ...observer.executionShape(cold ? ['model-load', 'first-generation'] : ['timed']),
    // Wall of one timed unit (per decoded token, the prefill generation, or
    // process start through the cold generation), for the comparator's
    // traced-versus-wall coverage check.
    processWallMs: unitWallMs,
    processWallScope: 'workload-unit',
    scenarioPath: args.scenarioPath,
    scenarioSha256,
    workloadMode: workload.mode,
    adapterInfo: observer.adapterInfo,
    backendSelection: observer.backendSelection,
    observedDeviceCount: observer.devices.length,
    packageProviderIdentity: provider.identity,
    ...observer.shaderReceipt(),
    asyncPipelineCreation: observer.asyncPipelineSummary(),
    modelCustody: custody,
    dopplerSource: source,
    generationOptions: options,
    dopplerExecution: {
      kernelPathId: stats.kernelPathId ?? null,
      kernelPathSource: stats.kernelPathSource ?? null,
      activationDtype: executionPlan?.activationDtype ?? null,
      readbackMode: executionPlan?.readbackMode ?? null,
      batchSize: executionPlan?.batchSize ?? null,
      decodeMode: stats.decodeMode ?? null,
    },
    metrics,
    resultSummary: {
      outputKind: 'generated-token-ids',
      outputSha256: sha256(outputBytes),
      outputByteLength: Buffer.byteLength(outputBytes),
      tokenIds: timed.tokens,
      promptTokenCount,
      referenceClass: workload.references?.[args.backend]?.referenceClass ?? null,
    },
    warmupGenerationCount: warmups.length,
  };
  try {
    checkReference(workload, args.backend, observed);
  } catch (error) {
    error.fields = fields;
    throw error;
  }
  fields.resultSummary.referenceMatched = true;
  return fields;
}

await runExecutor(USAGE, 'doppler', run);
