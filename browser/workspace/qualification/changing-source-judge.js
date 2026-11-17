// Fixed acceptance for the changing-source investigation. Pure and mechanical: it establishes that
// the records exist, are intact, are linked and rest on the sources as they are now. Whether the
// diagnosis is right is a person's judgement, recorded separately (see gradeDiagnosis).
import { sha256Hex } from '../src/journal.js';
import { DIAGNOSIS_UNGRADED } from './authenticated-investigation-judge.js';

export const ACCEPTANCE = Object.freeze([
  'the final run completed',
  'every evidence record still matches its saved bytes',
  'every source that changed between the two commits was read',
  'at least two competing explanations were recorded and at least one was supported or rejected with a reason',
  'no record is left marked as needing review',
  'no conclusion rests on a version of a source that no longer exists in the repository',
  'every conclusion that rested on a changing source when the first agent was interrupted was written again afterwards',
  'a finding rests on a changed source as it is now, with at least one open question and one decision',
]);

const bound = artifact => typeof artifact.source?.content === 'string' && artifact.source.sha256 === sha256Hex(artifact.source.content);
/** Evidence a record rests on, followed through every intermediate record. */
function roots(artifacts, artifact, seen = new Set()) {
  const found = [];
  for (const id of artifact.dependsOn ?? []) {
    const parent = artifacts[id];
    if (!parent || seen.has(id)) continue;
    seen.add(id);
    if (parent.kind === 'evidence') found.push(parent); else found.push(...roots(artifacts, parent, seen));
  }
  return found;
}

/**
 * @param options.liveHashes  path -> sha256 of the file in the repository at the end of the run
 * @param options.changedSources  investigated paths that differ between the two commits
 * @param options.restingOnChangingSource  ids of records that rested on such a source at the interruption
 * @param options.boundaries  { resumeStarted } activity sequence number
 */
export function judgeChangingSourceInvestigation(project, { liveHashes, changedSources, restingOnChangingSource, boundaries }) {
  const all = project.artifacts, artifacts = Object.values(all);
  const evidence = artifacts.filter(value => value.kind === 'evidence'), reasoning = artifacts.filter(value => value.kind !== 'evidence');
  const current = evidenceRecord => evidenceRecord.source?.kind !== 'repository' || !Object.hasOwn(liveHashes, evidenceRecord.source.path)
    || liveHashes[evidenceRecord.source.path] === evidenceRecord.source.sha256;
  const superseded = reasoning.filter(value => roots(all, value).some(source => !current(source)));
  const writtenAt = new Map();
  for (const event of project.activity) if (event.event === 'tool-result' && event.output?.id) writtenAt.set(event.output.id, event.seq);
  const hypotheses = reasoning.filter(value => value.kind === 'hypothesis');
  const settled = hypotheses.filter(value => ['supported', 'rejected'].includes(value.disposition?.state) && value.disposition.reason && value.dependsOn.length);
  const notRechecked = restingOnChangingSource.filter(id => !((writtenAt.get(id) ?? 0) > boundaries.resumeStarted));
  const onChangedNow = value => roots(all, value).some(source => source.source?.kind === 'repository' && changedSources.includes(source.source.path) && current(source));
  const checks = {
    finalCompleted: project.run?.status === 'completed',
    evidenceIntact: evidence.length > 0 && evidence.every(bound),
    changedSourcesRead: changedSources.every(path => evidence.some(value => value.source?.kind === 'repository' && value.source.path === path)),
    hypothesesSettled: hypotheses.length >= 2 && settled.length >= 1,
    noRecordNeedsReview: reasoning.every(value => value.status === 'current'),
    conclusionsOnCurrentSources: superseded.length === 0,
    interruptedConclusionsRechecked: notRechecked.length === 0,
    connectedDiagnosis: reasoning.some(value => value.kind === 'finding' && value.status === 'current' && onChangedNow(value))
      && reasoning.some(value => value.kind === 'question') && reasoning.some(value => value.kind === 'decision'),
  };
  return { checks, mechanicallyAccepted: Object.values(checks).every(Boolean), diagnosis: { ...DIAGNOSIS_UNGRADED },
    counts: { evidence: evidence.length, records: reasoning.length, hypotheses: hypotheses.length, settledHypotheses: settled.length,
      rejectedHypotheses: hypotheses.filter(value => value.disposition?.state === 'rejected').length,
      needingReview: reasoning.filter(value => value.status !== 'current').length, restingOnSupersededSource: superseded.length,
      restedOnChangingSourceAtInterruption: restingOnChangingSource.length, notWrittenAgain: notRechecked.length } };
}
