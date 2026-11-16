import assert from 'node:assert/strict';
import { handoffBrief } from '../../src/client/handoff.js';
const activity = [], artifacts = {};
for (let index = 0; index < 12000; index++) {
  const seq = index * 5;
  activity.push({ seq, event: 'delegated', task: `Task ${index}`, pageIds: ['tracker'], origins: ['https://tracker.example'] });
  activity.push({ seq: seq + 1, event: 'tool-requested', callId: `call-${index}`, tool: 'act', input: { text: 'x'.repeat(2000) } });
  if (index % 3) activity.push({ seq: seq + 2, event: 'tool-result', callId: `call-${index}`, output: { actionId: `call-${index}`, isUncertain: false } });
  activity.push({ seq: seq + 3, event: 'human-note', text: `Human note ${index}` });
  activity.push({ seq: seq + 4, event: 'finish', text: `Finished ${index}` });
  artifacts[`q-${index}`] = { id: `q-${index}`, revision: 1, kind: 'question', status: 'current', text: `Question ${index}` };
}
let visits = 0;
const scanned = new Proxy(activity, { get(target, key, receiver) {
  if (key === 'some') throw Error('Settlement must not rescan history per request');
  if (/^\d+$/.test(String(key))) visits++;
  return Reflect.get(target, key, receiver);
} });
const project = { activity: scanned, artifacts, run: { status: 'interrupted', turns: 8 } };
const workspace = { actions: Array.from({ length: 67 }, (_, index) => ({ actionId: `action-${index}`, pageId: 'tracker',
  kind: 'submit', isUncertain: true, target: { text: 'x'.repeat(2000) }, completion: { text: 'y'.repeat(2000) } })) };
const brief = handoffBrief(project, workspace);
assert.ok(visits <= activity.length * 2, `${visits} event visits for ${activity.length} events`);
assert.ok(JSON.stringify(brief).length < 60000, 'many sessions still fit a usable planner packet');
assert.equal(brief.unresolvedActionCount, 67);
assert.equal(brief.unresolvedActions.length, 20);
assert.equal(brief.pagination.unresolvedActions.omittedOlder, 47);
assert.match(brief.guidance, /67 earlier actions/);
assert.ok(brief.tried.some(value => value.result.interrupted));
const settled = brief.tried.find(value => value.result.actionId);
assert.equal(settled.callId, settled.result.actionId);
assert.equal(settled.resultSeq, settled.requestSeq + 1);
assert.equal(brief.sessions.at(-1).finishProposal.seq, brief.sessions.at(-1).seq + 4);
assert.ok(brief.unresolvedActions[0].target.truncated);
assert.ok(brief.human.at(-1).text.endsWith('11999'));
const seen = new Set();
let page = brief;
while (true) {
  for (const action of page.unresolvedActions) { assert.ok(!seen.has(action.actionId)); seen.add(action.actionId); }
  const cursor = page.pagination.unresolvedActions.nextCursor;
  if (cursor === null) break;
  page = handoffBrief(project, workspace, { cursors: { unresolvedActions: cursor } });
  assert.equal(page.unresolvedActionCount, 67);
}
assert.equal(seen.size, 67, 'every unresolved outcome can be retrieved without omission or duplication');
const older = handoffBrief(project, workspace, { cursors: { sessions: brief.pagination.sessions.nextCursor,
  tried: brief.pagination.tried.nextCursor, human: brief.pagination.human.nextCursor,
  openQuestions: brief.pagination.openQuestions.nextCursor } });
assert.ok(older.sessions.at(-1).seq < brief.sessions[0].seq);
assert.ok(older.tried.at(-1).requestSeq < brief.tried[0].requestSeq);
assert.throws(() => handoffBrief(project, workspace, { cursors: { unresolvedActions: 'no-such-action' } }), /refresh the handoff/);
const failedFinish = handoffBrief({ activity: [
  { seq: 1, event: 'delegated', task: 'Finish' },
  { seq: 2, event: 'finish', text: 'I am done' },
  { seq: 3, type: 'run-updated', run: { status: 'needs-attention', detail: 'An unresolved outcome remains' } },
], artifacts: {} });
assert.equal(failedFinish.sessions[0].finishProposal.how, 'finish');
assert.equal(failedFinish.sessions[0].ended.how, 'needs-attention');
assert.match(failedFinish.sessions[0].ended.detail, /unresolved/);
assert.equal(activity.length, 56000, 'the complete journal was not truncated');
console.log('ok: bounded handoff, linear settlement, complete pagination and provenance');
