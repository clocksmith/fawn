import assert from 'node:assert/strict';
import { buildPlanRefactorReceipt } from '../../scripts/build-plan-refactor-receipt.js';

// The historical receipt retains its source hashes. Live contracts exercise the API.
const receipt = await buildPlanRefactorReceipt();
assert.equal(receipt.artifactKind, 'doe_refactor_receipt');
assert.equal(receipt.classification, 'contract_tightening');
assert.equal(receipt.characterization.publicExportsPreserved, true);
assert.equal(receipt.status, 'pass');

process.stdout.write('plan-refactor-receipt: ok\n');
