// Fawn workspace — artifact export workflow adapter
//
// Task logic only: opens an export page and downloads the artifact a named link produces.

import { OUTCOMES } from '../state.js';
import { findElement } from './owner-session.js';

/**
 * @param {ReturnType<import('./owner-session.js').createOwnerSession>} session
 * @param {{runId: string, intent: string, exportUrl: string, linkText: string}} task
 */
async function runArtifactExport(session, { runId, intent, exportUrl, linkText }) {
  const steps = [];
  const opened = await session.act(await session.observe(), {
    actionId: `${runId}-open`,
    authorizedIntent: intent,
    kind: 'navigate',
    target: { url: exportUrl },
  });
  steps.push(opened);
  if (opened.outcome !== OUTCOMES.COMPLETED) return { status: 'stopped', steps };
  const page = await session.observe();
  const exported = await session.act(page, {
    actionId: `${runId}-download`,
    authorizedIntent: intent,
    kind: 'download',
    target: findElement(page, (element) => element.tag === 'a' && element.text === linkText, `link ${linkText}`),
  });
  steps.push(exported);
  return {
    status: exported.outcome ?? exported.dispatchStatus,
    artifact: exported.outcome === OUTCOMES.COMPLETED ? exported.evidence.effect : null,
    steps,
  };
}

export { runArtifactExport };
