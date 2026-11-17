// Planner input is paid for on every turn. A large page observation stays in full while it is being
// worked on and is then reduced, so reading more sources does not keep resending the whole page.
import assert from 'node:assert/strict';
import { investigationContext } from '../../src/client/agent.js';

const page = { pageId: 'main', url: 'https://docs.example/api', title: 'API', observationVersion: 4, isTruncated: false,
  text: 'Reference text. '.repeat(1200), elements: Array.from({ length: 200 }, (_, index) => ({ handle: `h${index}`, selector: `#link-${index}`, role: 'link', label: `Link ${index}`, href: `https://docs.example/${index}` })) };
let seq = 10;
const activity = [];
const call = (tool, input, output) => { const callId = `c${seq}`; activity.push({ seq: ++seq, event: 'tool-requested', callId, tool, input }, { seq: ++seq, event: 'tool-result', callId, output }); };
const context = () => investigationContext({ goal: 'g', artifacts: {}, activity, repositoryRoot: '/r' }, { task: 't', permissions: {}, initialArtifacts: new Set(), startSequence: 10, shareRecords: true });
const observed = () => context().history.find(event => event.event === 'tool-result' && event.output?.pageId === 'main').output;
const size = value => JSON.stringify(value).length;

call('observe', { pageId: 'main' }, page);
assert.equal(observed().elements.length, 200, 'the page just observed is sent in full');
const full = size(context());
for (let index = 0; index < 2; index++) call('record', { kind: 'question', text: `q${index}` }, { ok: true });
assert.equal(observed().elements.length, 200, 'and stays in full while the planner is still working near it');
call('record', { kind: 'question', text: 'q2' }, { ok: true });
const aged = observed();
assert.equal(aged.elements, undefined);
assert.deepEqual([aged.url, aged.title, aged.observationVersion, aged.elementCount, aged.textChars], [page.url, page.title, 4, 200, page.text.length]);
assert.equal(aged.textExcerpt.length, 1200);
assert.match(aged.aged, /Observe this page again/);
assert.ok(size(context()) < full / 10, `aged context ${size(context())} vs full ${full}`);
// Observing again restores the full view of the page as it is now.
call('observe', { pageId: 'main' }, { ...page, observationVersion: 9 });
const views = context().history.filter(event => event.event === 'tool-result' && event.output?.pageId === 'main').map(event => event.output);
assert.deepEqual(views.map(view => [view.observationVersion, Array.isArray(view.elements), !!view.superseded]), [[4, false, true], [9, true, false]]);
// A failed proposal counts as a result too, so a run of refusals still ages the page out.
for (let index = 0; index < 3; index++) { const callId = `f${index}`; activity.push({ seq: ++seq, event: 'tool-requested', callId, tool: 'act', input: {} }, { seq: ++seq, event: 'tool-failed', callId, error: 'refused', recoverable: true }); }
assert.equal(context().history.findLast(event => event.output?.observationVersion === 9).output.elements, undefined);
console.log('ok: a page observation is sent in full while in use, reduced after three later results, and restored by observing again');
