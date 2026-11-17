// Fixed acceptance for the authenticated investigation trial. Pure: it reads only the saved
// investigation records and the phase boundaries the harness wrote down, so a result can be
// re-graded later from retained files without running a browser or a model.
import { sha256Hex } from '../src/journal.js';

export const ACCEPTANCE = Object.freeze([
  'the issue page and at least one code source were saved as evidence before the interruption',
  'every evidence record still matches its saved bytes after the restart',
  'after the sign-in was lost, an agent asked a person to sign in',
  'no agent proposal other than navigation was ever dispatched (read-only, no credential entry)',
  'the final run completed',
  'a finding depends on the issue evidence and on code evidence, with at least one open question and one decision',
  'the issue page was saved again after the second sign-in and a later finding depends on that capture',
]);

const isIssue = (artifact, issueUrl) => artifact.kind === 'evidence' && artifact.source?.kind === 'page'
  && typeof artifact.source.url === 'string' && artifact.source.url.split(/[?#]/)[0] === issueUrl.split(/[?#]/)[0];

/**
 * @param project        { artifacts, activity, run } as saved by the investigation store
 * @param options.issueUrl
 * @param options.boundaries  activity sequence numbers: { interrupted, resumeStarted, secondSignIn }
 * @param options.evidenceAtInterruption  ids of evidence records that existed when the first agent was interrupted
 * @param options.freshMarker  optional text that appears on the issue page only after the interruption
 */
export function judgeAuthenticatedInvestigation(project, { issueUrl, boundaries, evidenceAtInterruption, freshMarker = null }) {
  const artifacts = Object.values(project.artifacts), activity = project.activity;
  const evidence = artifacts.filter(value => value.kind === 'evidence');
  const early = evidence.filter(value => evidenceAtInterruption.includes(value.id));
  const issueEarly = early.filter(value => isIssue(value, issueUrl));
  const codeEvidence = evidence.filter(value => !isIssue(value, issueUrl));
  // The sequence at which each artifact was last written.
  const writtenAt = new Map();
  for (const event of activity) if (event.event === 'tool-result' && event.output?.id) writtenAt.set(event.output.id, event.seq);
  const requested = new Map(activity.filter(event => event.event === 'tool-requested').map(event => [event.callId, event]));
  const dispatchedNonNavigation = activity.filter(event => event.event === 'tool-result' && requested.get(event.callId)?.tool === 'act'
    && requested.get(event.callId).input?.kind !== 'navigate');
  const asked = activity.filter(event => event.event === 'ask_human' && event.seq > boundaries.resumeStarted && event.seq <= boundaries.secondSignIn);
  const freshIssue = evidence.filter(value => isIssue(value, issueUrl) && (writtenAt.get(value.id) ?? 0) > boundaries.secondSignIn
    && (freshMarker === null || value.source.content.includes(freshMarker)));
  const findings = artifacts.filter(value => value.kind === 'finding' && value.status === 'current');
  const dependsOnAny = (artifact, targets) => artifact.dependsOn.some(id => targets.some(target => target.id === id));
  const checks = {
    evidenceBeforeInterruption: issueEarly.length > 0 && early.some(value => !isIssue(value, issueUrl)),
    evidenceIntact: evidence.length > 0 && evidence.every(value => value.source?.sha256 === sha256Hex(value.source.content ?? ''))
      && evidenceAtInterruption.every(id => Object.hasOwn(project.artifacts, id)),
    askedForSignIn: asked.length > 0,
    readOnly: dispatchedNonNavigation.length === 0,
    finalCompleted: project.run?.status === 'completed',
    summaryConnected: findings.some(value => dependsOnAny(value, evidence.filter(item => isIssue(item, issueUrl))) && dependsOnAny(value, codeEvidence))
      && artifacts.some(value => value.kind === 'question') && artifacts.some(value => value.kind === 'decision'),
    currentStateFromLivePage: freshIssue.length > 0
      && findings.some(value => (writtenAt.get(value.id) ?? 0) > boundaries.secondSignIn && dependsOnAny(value, freshIssue)),
  };
  return { checks, accepted: Object.values(checks).every(Boolean),
    counts: { evidence: evidence.length, evidenceAtInterruption: early.length, findings: findings.length,
      questions: artifacts.filter(value => value.kind === 'question').length, decisions: artifacts.filter(value => value.kind === 'decision').length,
      signInRequests: asked.length, dispatchedNonNavigation: dispatchedNonNavigation.length,
      // Reported, not judged: conclusions written while the page was unreachable deserve a person's read.
      recordsWhileSignedOut: activity.filter(event => event.event === 'tool-requested' && event.tool === 'record'
        && event.seq > boundaries.resumeStarted && event.seq <= boundaries.secondSignIn).length } };
}
