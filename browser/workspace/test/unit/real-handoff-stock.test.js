import assert from 'node:assert/strict';
import { validateStockProposal, recoverStockProposal } from '../../qualification/real-handoff-stock.js';
let browserCalls = 0;
const page = new Proxy({}, { get() { browserCalls++; throw Error('Browser API began'); } });
const pages = new Map([['main', page]]), observations = new Map([['main', { elements: [{ handle: 'h1', selector: '#post' }] }]]);
for (const [tool, input] of [['act', { kind: 'unsupported' }], ['act', { kind: 'click', target: { handle: 'missing' } }],
  ['act', { kind: 'navigate', target: { url: 'invalid' } }], ['observe', { pageId: 'missing' }], ['handoff_read', {}],
  ['act', { kind: 'fill', target: { handle: 'h1' }, params: { value: {} } }]]) {
  assert.throws(() => validateStockProposal(tool, input, pages, observations), error => recoverStockProposal(error));
}
assert.equal(browserCalls, 0, 'recoverable attestation precedes every Playwright call');
assert.equal(recoverStockProposal(Error('Failure after a click may have begun')), false);
assert.equal(recoverStockProposal({ code: 'STOCK_PROPOSAL_INVALID' }), false);
assert.equal(validateStockProposal('act', { kind: 'click', target: { handle: 'h1' } }, pages, observations).element.selector, '#post');
assert.equal(validateStockProposal('artifact_read', {}, pages, observations).page, page);
console.log('ok: stock proposal recovery is limited to attested pre-browser failures');
