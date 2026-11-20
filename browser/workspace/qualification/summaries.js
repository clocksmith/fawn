// Fawn workspace qualification — plain summaries shared by the runner and the validator.
//
// The runner summarizes what callers saw; the validator summarizes the same actions from the
// journal it replays. Both use these shapes so expectations compare one form.

function summarizeAction(action) {
  if (!action) return null;
  return {
    dispatchStatus: action.dispatchStatus,
    outcome: action.outcome,
    outcomeBasis: action.outcomeBasis,
    isUncertain: action.isUncertain,
    neverDispatchedCode: action.neverDispatched?.code ?? null,
    neverDispatchedStage: action.neverDispatched?.stage ?? null,
    reconciliationResult: action.reconciliation?.result ?? null,
    acknowledged: action.acknowledgedGeneration !== null,
    ownerGeneration: action.ownerGeneration,
  };
}

const WORKFLOW_SUMMARIES = Object.freeze({
  research: (result) => ({
    findingTitles: result.findings.map((finding) => finding.title),
    skipped: result.skipped.length,
  }),
  form: (result) => ({ recovered: result.recovered ?? null }),
  export: (result) => ({ artifactSha256: result.artifact?.sha256 ?? null, recovered: result.recovered ?? null }),
});

function summarizeWorkflow(workflow, result) {
  return { status: result.status, ...WORKFLOW_SUMMARIES[workflow](result) };
}

/** A rejected call keeps its code; a host that died keeps its exit signal. */
function summarizeError(error) {
  return { rejection: error.code ?? null, message: error.message, ...(error.exit ? { exit: error.exit } : {}) };
}

/** Per-outcome counters use the actual model-provider call log. An unavailable
 * log or a call without measured usage makes spend unknown. Rules are counted
 * separately so a local model call is still charged as a planner call. */
function summarizeResidentOutcome({ elapsedMs, verified, ruleEvaluations, modelCalls, captures }) {
  const priced = Array.isArray(modelCalls) && modelCalls.every(call =>
    typeof call?.usage?.costUsd === 'number' && Number.isFinite(call.usage.costUsd) && call.usage.costUsd >= 0);
  return {
    elapsedMs, verified, ruleEvaluations,
    plannerCalls: Array.isArray(modelCalls) ? modelCalls.length : null,
    modelSpendUsd: priced ? modelCalls.reduce((total, call) => total + call.usage.costUsd, 0) : null,
    canvasObservations: captures.length,
    permissionRequests: captures.reduce((total, sample) => total + (sample.permissionRequests ?? 0), 0),
    consumedFrames: captures.reduce((total, sample) => total + (sample.consumedFrames ?? 0), 0),
    discardedFrames: captures.reduce((total, sample) => total + (sample.discardedFrames ?? 0), 0),
    cpuPixelReadBytes: captures.reduce((total, sample) => total + sample.cpuPixelReadBytes, 0),
    frameHostCopyBytes: captures.reduce((total, sample) => total + (sample.frameHostCopyBytes ?? 0), 0),
    cdpPixelPayloadBytes: captures.reduce((total, sample) => total + (sample.cdpPixelPayloadBytes ?? 0), 0),
    nativeHelperInputBytes: captures.reduce((total, sample) => total + (sample.nativeHelperInputBytes ?? 0), 0),
    peakNativeRgbaBufferBytes: captures.reduce((peak, sample) => Math.max(peak, sample.nativePeakRgbaBufferBytes ?? 0), 0),
    nativeHashedBytes: captures.reduce((total, sample) => total + (sample.nativeHashedBytes ?? 0), 0),
    logicalTextureWriteBytes: captures.reduce((total, sample) => total + (sample.externalTextureCopyBytes ?? 0), 0),
    mappedTileReadbackBytes: captures.reduce((total, sample) => total + (sample.mappedReadbackBytes ?? 0), 0),
    gpuMaskReadbackBytes: captures.reduce((total, sample) => total + (sample.gpuMaskReadbackBytes ?? 0), 0),
  };
}

export { summarizeAction, summarizeError, summarizeWorkflow, summarizeResidentOutcome };
