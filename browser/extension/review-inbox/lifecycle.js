// The life of one inbox item. This is the only definition of the states an item
// can be in, the moves allowed between them, which states are final, and the
// words the inbox uses for why something happened. The worker and the inbox
// page import it. page.js is a content script and cannot import anything: it
// holds elements by key and knows no state names.
//
// Everything here is a value or a pure function, so it runs without a browser.

export const STATE = Object.freeze({
  PENDING: 'pending',         // picked; waiting for a person to approve it
  APPROVED: 'approved',       // approved; waiting for a person to finish it
  FINISHING: 'finishing',     // one Finish request owns it and is clicking
  UNKNOWN: 'unknown',         // the click was or may have been sent and its result is not known; nothing is clicked until it is reconciled
  RECONCILING: 'reconciling', // one Reconcile request owns it and is reading the application's own record, without writing
  UNAPPLIED: 'unapplied',     // the application's record shows the click was not applied; only a person approving again reopens it
  BLOCKED: 'blocked',         // closed: nothing was sent
  REFUSED: 'refused',         // closed: the click was sent and the application said no
  CONFIRMED: 'confirmed',     // closed: the application shows it done
});
// Not a state an item is ever stored in: the move that discards it. It is in the table so that discarding
// an item obeys the table like every other move.
export const REMOVED = 'removed';

const { PENDING, APPROVED, FINISHING, UNKNOWN, RECONCILING, UNAPPLIED, BLOCKED, REFUSED, CONFIRMED } = STATE;
export const TRANSITIONS = Object.freeze({
  [PENDING]: [APPROVED, BLOCKED, REMOVED],
  [APPROVED]: [FINISHING, BLOCKED, REMOVED],
  // Only the request that is clicking ends a finish, so an item cannot be discarded from under it.
  [FINISHING]: [BLOCKED, REFUSED, CONFIRMED, UNKNOWN],
  // A result that is not known is not a failure. Nothing leads from here to a click: not Finish, not a
  // retry, not approval. The only way on is a read of the application's record.
  [UNKNOWN]: [RECONCILING, REMOVED],
  // Only the request that is reading ends a reconciliation, and it can only say what it read.
  [RECONCILING]: [CONFIRMED, UNAPPLIED, UNKNOWN],
  // Not applied: a person may approve exactly this again, after the same checks as the first time.
  [UNAPPLIED]: [APPROVED, REMOVED],
  [BLOCKED]: [REMOVED],
  [REFUSED]: [REMOVED],
  [CONFIRMED]: [REMOVED],
});

export const mayMove = (from, to) => TRANSITIONS[from]?.includes(to) ?? false;
// Open: a person can still check, approve or finish it, and it closes with its tab or page. Terminal: it has
// its result and can only be discarded. An item that is being finished or reconciled, or whose result is
// not known, is neither.
export const isOpen = state => state === PENDING || state === APPROVED;
export const isTerminal = state => TRANSITIONS[state]?.every(to => to === REMOVED) ?? false;
// States in which a fresh look at the reviewed control means something: the item may still be approved or
// finished.
export const isCheckable = state => isOpen(state) || state === UNAPPLIED;

export const REASON = Object.freeze({
  // What a check of the reviewed control found.
  matches: 'Matches what was reviewed.',
  tabClosed: 'The tab was closed.',
  documentReplaced: 'The page was reloaded or navigated.',
  controlGone: 'The reviewed control is no longer on the page.',
  covered: 'Something else now sits under the reviewed click point.',
  noCapture: 'The browser could not capture the reviewed area.',
  pageSilent: 'The page did not answer in time, so it could not be checked.',
  pixelsChanged: 'The reviewed area changed after it was reviewed.',
  // What a check of the application's own record found: the target and values the approval was for.
  stale: changes => `Stale approval: ${changes.join('; ')}.`,
  recordUnread: message => `The application's record could not be read, so the approval could not be checked: ${message}`,
  // What happened to a click.
  fenced: 'The click did not land on the reviewed control, so it was stopped.',
  noKnownSign: 'This kind of control has no known sign of its result.',
  neitherSign: 'The application showed neither success nor refusal. Nothing will be clicked again until this is reconciled.',
  workerStopped: 'The inbox stopped while this click was in progress. Nothing will be clicked again until this is reconciled.',
  browserStopped: 'The browser stopped while this click was in progress. Nothing will be clicked again until this is reconciled.',
  browserRestarted: 'The browser was restarted, so the reviewed page is gone.',
  lostTrack: message => `The inbox lost track of this click: ${message}`,
  answerLost: requests => `The click's request went out but its answer did not reach the page (${requests}). It may or may not have been applied. Nothing will be clicked again until this is reconciled.`,
  // What a reconciliation found.
  applied: evidence => `The application's record shows it done: ${evidence}.`,
  notApplied: evidence => `The application's record shows it was not done: ${evidence}.`,
  termsHold: 'The approved target and values still hold. Approve again to finish it; nothing is clicked until you do.',
  noRecord: 'This kind of control has no read-only record to check. Check the application yourself; nothing will be clicked again.',
  unreadable: message => `The application's record could not be read: ${message}. Nothing will be clicked again until this is reconciled.`,
  tooSoon: seconds => `The record does not show it done yet, but the click went out only ${seconds} s ago and may still be in progress. Reconcile again later.`,
  readsDisagree: 'Two reads of the record a moment apart did not agree. Reconcile again later.',
  reconcileStopped: 'The inbox stopped while reading the record. Nothing was written. Reconcile again.',
  // Why a request was refused.
  notPending: 'This item is not waiting for approval.',
  notApproved: 'This item is not approved, or it is already being finished.',
  unknownNoFinish: 'The result of the last click is not known. Reconcile it before anything is clicked again.',
  unknownNoApprove: 'The result of the last click is not known. Reconcile it before approving again.',
  notUnknown: 'Only an item whose result is not known can be reconciled, and only by one request at a time.',
  reviewAgain: reason => `${reason} Review it again before approving.`,
  removeWhileFinishing: 'This item is being clicked right now. Wait for its result, then clear it.',
  removeWhileReconciling: 'This item\'s record is being read right now. Wait for the result, then clear it.',
});
export const OUTCOME = Object.freeze({
  blocked: 'Click blocked',
  refused: 'App refused',
  notConfirmed: noun => `Click sent; ${noun} not confirmed`,
  maybeSent: noun => `Click may have been sent; ${noun} not confirmed`,
  stillUnknown: noun => `Still unknown whether the ${noun} happened`,
  notApplied: noun => `Not applied: no ${noun} happened`,
});

// Why an item in state `from` may not move to `to`, in words for the person; null when it may.
export function refusal(from, to) {
  if (mayMove(from, to)) return null;
  if (from === UNKNOWN || from === RECONCILING) {
    if (to === FINISHING) return REASON.unknownNoFinish;
    if (to === APPROVED) return REASON.unknownNoApprove;
  }
  if (to === APPROVED) return REASON.notPending;
  if (to === FINISHING) return REASON.notApproved;
  if (to === RECONCILING) return REASON.notUnknown;
  if (to === REMOVED && from === FINISHING) return REASON.removeWhileFinishing;
  if (to === REMOVED && from === RECONCILING) return REASON.removeWhileReconciling;
  return `An item that is ${from} cannot become ${to}.`;
}

// What an item becomes when the thing it is bound to goes away. An open item closes as blocked, with the
// reason. Anything else is left alone (null): a terminal item already has its result, an item that is
// being finished belongs to that finish, which alone knows whether the click went out, and an item whose
// result is not known stays not known whatever its tab does.
const closed = (item, reason, at) => isOpen(item.state)
  ? { state: BLOCKED, check: { at, holds: false, reason }, outcome: { text: OUTCOME.blocked, detail: reason, at } } : null;
export const whenTabCloses = (item, at) => closed(item, REASON.tabClosed, at);
// `documentId` is the document the item's tab shows now.
export const whenDocumentIs = (item, documentId, at) => documentId === item.documentId ? null : closed(item, REASON.documentReplaced, at);

// A reconciliation waits this long after the click went out before it will believe a record that shows
// nothing happened, and then reads twice, this far apart: a missing response, or an immediate read that
// finds nothing, does not prove no write occurred.
export const SETTLE_MS = 30000, REREAD_MS = 1000;
// What a reconciliation concludes from what it read. `found` is one of
//   { unsupported: true }                         the control has no read-only record
//   { failure: message }                          the record could not be read
//   { reads: [view, ...], sinceSentMs }           what each read showed, in order, where a view is
//     { applied: boolean, changes: [text], evidence: text, signature: text }
// `noun` and `doneLabel` come from the control's recipe. The answer is always one of the three moves the
// table allows from `reconciling`; none of them is a click.
export function reconciled(found, { noun, doneLabel }, at) {
  const unknown = detail => ({ state: UNKNOWN, outcome: { text: OUTCOME.stillUnknown(noun), detail, at } });
  if (found.unsupported) return unknown(REASON.noRecord);
  if (found.failure) return unknown(REASON.unreadable(found.failure));
  const last = found.reads.at(-1);
  if (found.reads.some(view => view.applied)) {
    const done = found.reads.find(view => view.applied);
    return { state: CONFIRMED, outcome: { text: doneLabel, detail: REASON.applied(done.evidence), at } };
  }
  if (found.sinceSentMs < SETTLE_MS) return unknown(REASON.tooSoon(Math.max(0, Math.floor(found.sinceSentMs / 1000))));
  if (found.reads.length < 2 || found.reads.some(view => view.signature !== last.signature)) return unknown(REASON.readsDisagree);
  const holds = last.changes.length === 0;
  return { state: UNAPPLIED, outcome: { text: OUTCOME.notApplied(noun), detail: REASON.notApplied(last.evidence), at },
    check: { at, holds: holds ? null : false, reason: holds ? REASON.termsHold : REASON.stale(last.changes) } };
}
