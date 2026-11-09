export function investigationContext(project, { task, permissions, initialArtifacts, startSequence, shareRecords }) {
  return { goal: project.goal, task, permissions, repositoryAvailable: !!project.repositoryRoot,
    artifacts: Object.fromEntries(Object.entries(project.artifacts).filter(([key]) => shareRecords || !initialArtifacts.has(key))),
    history: project.activity.filter(event => event.seq > startSequence) };
}

// A bounded product agent. It owns planning, never browser authority or outcome truth.
import { randomUUID } from 'node:crypto';

export function startAgent({ projectId, store, planner, execute, config, signal, assertCurrent, context: getContext }) {
  const id = randomUUID(), usage = [];
  let turns = 0;
  const save = (status, detail) => {
    if (store.get(projectId).run?.id !== id) return;
    store.run(projectId, { id, status, detail, turns, usage, planner: planner.identity });
  };
  store.run(projectId, { id, status: 'running', turns, usage, planner: planner.identity });
  const done = (async () => {
    try {
      while (turns < config.maxTurns) {
        assertCurrent(); signal.throwIfAborted();
        const context = getContext();
        if (Buffer.byteLength(JSON.stringify(context)) > config.maxInputBytes) throw Error('Investigation input budget reached');
        const requestId = randomUUID();
        store.activity(projectId, { event: 'planner-requested', requestId, planner: planner.identity });
        const proposal = await planner.next(context, { signal });
        turns += 1; usage.push(proposal.usage ?? null);
        store.activity(projectId, { event: 'planner-result', requestId, usage: proposal.usage ?? null });
        const current = store.get(projectId).run;
        if (current?.id === id) store.run(projectId, { ...current, turns, usage });
        assertCurrent(); signal.throwIfAborted();
        const callId = randomUUID();
        // Persist the call before execution. Restart never automatically replays a pending call.
        store.activity(projectId, { event: 'tool-requested', callId, tool: proposal.tool, input: proposal.input });
        if (proposal.tool === 'finish' || proposal.tool === 'ask_human') {
          store.activity(projectId, { event: proposal.tool, text: proposal.input.summary ?? proposal.input.reason ?? '' });
          save(proposal.tool === 'finish' ? 'completed' : 'needs-attention', proposal.input.summary ?? proposal.input.reason);
          return;
        }
        const output = await execute(proposal.tool, proposal.input, { callId });
        store.activity(projectId, { event: 'tool-result', callId, output });
        assertCurrent(); signal.throwIfAborted();
        if (output?.isUncertain || output?.outcome === 'failed' || output?.serviceError) {
          save('needs-attention', 'The last action needs review. No retry was attempted.'); return;
        }
        save('running', `Finished ${proposal.tool}`);
      }
      save('needs-attention', 'Task turn budget reached');
    } catch (error) {
      save(signal.aborted ? 'stopped' : 'needs-attention', signal.aborted ? 'Human control requested; dispatched effects may still settle.' : error.message);
    }
  })();
  return { id, done };
}
