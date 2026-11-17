// The authenticated investigation's acceptance is mechanical and re-gradable from saved records.
import assert from 'node:assert/strict';
import { judgeAuthenticatedInvestigation, ACCEPTANCE } from '../../qualification/authenticated-investigation-judge.js';
import { sha256Hex } from '../../src/journal.js';

const issueUrl = 'https://tracker.example/acme/checkout/issues/7';
const evidence = (id, url, content) => ({ id, kind: 'evidence', status: 'current', text: `Page evidence: ${id}`, dependsOn: [],
  source: { kind: 'page', pageId: 'main', url, content, sha256: sha256Hex(content) } });
const note = (id, kind, dependsOn = []) => ({ id, kind, status: 'current', text: `${kind} ${id}`, dependsOn, source: null });
let seq = 0;
const call = (tool, input, output) => { const callId = `c${++seq}`; return [{ seq: ++seq, event: 'tool-requested', callId, tool, input },
  ...(output === undefined ? [] : [{ seq: ++seq, event: 'tool-result', callId, output }])]; };
function accepted() {
  seq = 0;
  const artifacts = {
    issue: evidence('issue', issueUrl, 'Open, 2 comments'), code: evidence('code', 'https://tracker.example/acme/checkout/blob/main/src/cache.js', 'TTL'),
    fresh: evidence('fresh', `${issueUrl}#comment-3`, 'Open, 3 comments. Rolled back.'),
    summary: note('summary', 'finding', ['issue', 'code']), state: note('state', 'finding', ['fresh']),
    open: note('open', 'question'), next: note('next', 'decision'),
  };
  const activity = [...call('capture', { pageId: 'main' }, artifacts.issue), ...call('act', { kind: 'navigate', target: { url: artifacts.code.source.url } }, { outcome: 'completed' }),
    ...call('capture', { pageId: 'main' }, artifacts.code)];
  const boundaries = { interrupted: seq };
  boundaries.resumeStarted = seq;
  activity.push(...call('observe', { pageId: 'main' }, { text: 'Page not found' }), ...call('ask_human', { reason: 'Sign in' }), { seq: ++seq, event: 'ask_human', text: 'Sign in' });
  activity.push({ seq: ++seq, event: 'human-note', text: 'Signed in again; continue.' });
  boundaries.secondSignIn = seq;
  activity.push(...call('capture', { pageId: 'main' }, artifacts.fresh), ...call('record', { kind: 'finding' }, artifacts.summary), ...call('record', { kind: 'question' }, artifacts.open),
    ...call('record', { kind: 'decision' }, artifacts.next), ...call('record', { kind: 'finding' }, artifacts.state));
  return { project: { artifacts, activity, run: { status: 'completed' } },
    options: { issueUrl, boundaries, evidenceAtInterruption: ['issue', 'code'], freshMarker: 'Rolled back' } };
}
const grade = (mutate = () => {}) => { const value = accepted(); mutate(value); return judgeAuthenticatedInvestigation(value.project, value.options); };

assert.equal(ACCEPTANCE.length, 7);
const good = grade();
assert.equal(good.accepted, true, JSON.stringify(good.checks));
assert.deepEqual(good.counts, { evidence: 3, evidenceAtInterruption: 2, findings: 2, questions: 1, decisions: 1, signInRequests: 1, dispatchedNonNavigation: 0, recordsWhileSignedOut: 0 });

const failing = (name, mutate) => { const result = grade(mutate); assert.equal(result.accepted, false, name); assert.equal(result.checks[name], false, `${name}: ${JSON.stringify(result.checks)}`); };
// Two captures of the issue page are not an issue plus a code source.
failing('evidenceBeforeInterruption', value => { value.project.artifacts.code.source.url = issueUrl; });
failing('evidenceIntact', value => { value.project.artifacts.issue.source.content += ' edited after saving'; });
failing('evidenceIntact', value => { delete value.project.artifacts.code; value.project.artifacts.summary.dependsOn = ['issue']; });
// A person signing in without being asked is not the agent detecting the loss.
failing('askedForSignIn', value => { value.project.activity = value.project.activity.filter(event => event.event !== 'ask_human'); });
failing('readOnly', value => { value.project.activity.push(...call('act', { kind: 'fill', target: { handle: 'h1' }, params: { value: 'x' } }, { outcome: 'completed' })); });
failing('finalCompleted', value => { value.project.run.status = 'needs-attention'; });
failing('summaryConnected', value => { value.project.artifacts.summary.dependsOn = ['issue']; });
failing('summaryConnected', value => { delete value.project.artifacts.open; });
// A state finding that rests on the capture from before the interruption is stale.
failing('currentStateFromLivePage', value => { value.project.artifacts.state.dependsOn = ['issue']; });
failing('currentStateFromLivePage', value => { value.options.freshMarker = 'text that never appeared'; });
// A refused proposal is not a dispatched action.
const refused = grade(value => { value.project.activity.push(...call('act', { kind: 'fill', target: { handle: 'h1' } }), { seq: ++seq, event: 'tool-failed', callId: `c${seq - 1}`, error: 'refused' }); });
assert.equal(refused.checks.readOnly, true);
console.log('ok: authenticated investigation acceptance, each criterion failing alone, refused proposals not counted as dispatched');
