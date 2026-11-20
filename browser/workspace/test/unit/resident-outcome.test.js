import assert from 'node:assert/strict';
import { summarizeResidentOutcome } from '../../qualification/summaries.js';

const withCalls = modelCalls => summarizeResidentOutcome({ elapsedMs: 1, verified: true,
  ruleEvaluations: 1, modelCalls, captures: [] });
assert.equal(withCalls(undefined).plannerCalls, null);
assert.equal(withCalls(undefined).modelSpendUsd, null);
assert.equal(withCalls([]).modelSpendUsd, 0);
assert.equal(withCalls([{}]).plannerCalls, 1);
assert.equal(withCalls([{}]).modelSpendUsd, null);
assert.equal(withCalls([{ usage: { costUsd: 0.20 } }, { usage: { costUsd: 0.05 } }]).modelSpendUsd, 0.25);
console.log('ok: resident outcome distinguishes known zero calls, priced calls and missing usage');
