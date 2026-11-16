// What a returning agent or person needs to continue an investigation without
// losing work or repeating an action whose outcome is unknown. Built only from the
// investigation journal and the service's action records; it grants no authority.

const MAX_TRIED = 40;
const MAX_TEXT = 400;

const clip = (value) => {
  const text = typeof value === 'string' ? value : JSON.stringify(value ?? null);
  return text.length > MAX_TEXT ? `${text.slice(0, MAX_TEXT)}…` : text;
};

function summarizeOutput(output) {
  if (output == null) return null;
  if (output.id && output.kind) return { artifactId: output.id, kind: output.kind };
  if (output.actionId) {
    return { actionId: output.actionId, kind: output.kind ?? null, outcome: output.outcome ?? null,
      basis: output.outcomeBasis ?? null, isUncertain: !!output.isUncertain };
  }
  if (output.error) return { error: clip(output.error) };
  return { summary: clip(output) };
}

/** Prior sessions: delegations, how each ended, what was tried and what the human did. */
export function handoffBrief(project, workspace = null) {
  const sessions = [];
  const tried = [];
  const human = [];
  const requested = new Map();
  let current = null;
  for (const event of project.activity) {
    if (event.event === 'delegated') {
      current = { task: clip(event.task), startedAt: event.at, actionKinds: event.actionKinds,
        pageIds: event.pageIds, origins: event.origins, ended: null };
      sessions.push(current);
    } else if (event.event === 'finish' || event.event === 'ask_human') {
      if (current) current.ended = { how: event.event, text: clip(event.text) };
    } else if (event.event === 'tool-requested') {
      requested.set(event.callId, { tool: event.tool, input: event.input });
    } else if (event.event === 'tool-result' || event.event === 'tool-failed') {
      const call = requested.get(event.callId);
      if (call) {
        tried.push({ tool: call.tool, input: clip(call.input),
          result: event.event === 'tool-failed' ? { error: clip(event.error) } : summarizeOutput(event.output) });
      }
    } else if (event.event === 'human-control') {
      human.push({ at: event.at, event: 'took control', outstandingActions: event.outstanding?.length ?? 0 });
    } else if (event.event === 'human-note') {
      human.push({ at: event.at, event: 'note', text: clip(event.text) });
    }
  }
  for (const [callId, call] of requested) {
    const settled = project.activity.some(event => event.callId === callId
      && (event.event === 'tool-result' || event.event === 'tool-failed'));
    if (!settled && !['finish', 'ask_human'].includes(call.tool)) {
      tried.push({ tool: call.tool, input: clip(call.input), result: { interrupted: 'no recorded result; the session ended first' } });
    }
  }
  const artifacts = Object.values(project.artifacts);
  const unresolved = (workspace?.actions ?? [])
    .filter(action => action.isUncertain)
    .map(action => ({
      actionId: action.actionId, pageId: action.pageId, kind: action.kind, target: action.target,
      effectKey: action.effectKey, completion: action.completion, outcome: action.outcome,
      basis: action.outcomeBasis, lastReconciliation: action.reconciliation?.result ?? null,
    }));
  return {
    previousRun: project.run ? { status: project.run.status, detail: clip(project.run.detail ?? ''), turns: project.run.turns } : null,
    sessions,
    tried: tried.slice(-MAX_TRIED),
    human,
    unresolvedActions: unresolved,
    openQuestions: artifacts.filter(value => value.kind === 'question' && value.status === 'current')
      .map(value => ({ id: value.id, text: clip(value.text) })),
    needsReview: artifacts.filter(value => value.status === 'needs-review').map(value => value.id),
    guidance: unresolved.length
      ? 'Some earlier actions have unknown outcomes. Establish each with reconcile (conditions observable on its page) before any new action; never repeat one unless reconcile returns effect-absent.'
      : 'No earlier action has an unknown outcome.',
  };
}
