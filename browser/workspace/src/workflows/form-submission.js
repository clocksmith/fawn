// Fawn workspace — controlled form submission workflow adapter
//
// Task logic only: fills named fields and submits once. The submission is an external effect
// identified by an effect key; retrying after an unknown outcome is the service's decision.

import { canonicalJson, sha256Hex } from '../journal.js';
import { OUTCOMES } from '../state.js';
import { findElement } from './owner-session.js';

function formEffectKey(formUrl, fields) {
  return `form:${sha256Hex(canonicalJson({ formUrl, fields }))}`;
}

/**
 * @param {ReturnType<import('./owner-session.js').createOwnerSession>} session
 * @param {{runId: string, intent: string, formUrl: string, fields: Record<string, string>,
 *   completedWhen: object[], failedWhen?: object[], externalIdempotencyKey?: string,
 *   resubmissionOf?: string}} task
 */
async function runFormSubmission(session, task) {
  const { runId, intent, formUrl, fields, completedWhen, failedWhen = [] } = task;
  const steps = [];
  const effectKey = formEffectKey(formUrl, fields);
  const proceed = (record) => {
    steps.push(record);
    return record.outcome === OUTCOMES.COMPLETED;
  };

  const start = await session.observe();
  if (start.url !== formUrl && !proceed(await session.act(start, {
    actionId: `${runId}-open`,
    authorizedIntent: intent,
    kind: 'navigate',
    target: { url: formUrl },
  }))) {
    return { status: 'stopped', effectKey, steps };
  }

  const fieldSelectors = {};
  for (const [name, value] of Object.entries(fields)) {
    const current = await session.observe();
    const target = findElement(current, (element) => element.attributes.some(([key, val]) => key === 'name'
      && val === name), `field ${name}`);
    fieldSelectors[name] = target.selector;
    const record = await session.act(current, {
      actionId: `${runId}-fill-${name}`,
      authorizedIntent: intent,
      kind: 'fill',
      target,
      params: { value },
    });
    if (!proceed(record)) return { status: 'stopped', effectKey, steps };
  }

  const ready = await session.observe();
  const effect = { external: true, effectKey };
  if (task.externalIdempotencyKey) effect.externalIdempotencyKey = task.externalIdempotencyKey;
  const submission = await session.act(ready, {
    actionId: `${runId}-submit`,
    authorizedIntent: intent,
    kind: 'submit',
    target: findElement(ready, (element) => element.formAction && element.attributes.some(([key, val]) => key === 'type'
      && val === 'submit'), 'submit control'),
    preconditions: Object.entries(fields).map(([name, value]) => ({
      kind: 'input-value-equals',
      selector: fieldSelectors[name],
      value,
    })),
    effect,
    completion: { completedWhen, failedWhen },
    resubmissionOf: task.resubmissionOf,
  });
  steps.push(submission);
  return { status: submission.outcome ?? submission.dispatchStatus, effectKey, submission, steps };
}

export { formEffectKey, runFormSubmission };
