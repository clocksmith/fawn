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
  BLOCKED: 'blocked',         // closed: nothing was sent
  REFUSED: 'refused',         // closed: the click was sent and the application said no
  CONFIRMED: 'confirmed',     // closed: the click was sent and the application shows it done
  UNCONFIRMED: 'unconfirmed', // closed: the click was or may have been sent, with no sign either way
});
// Not a state an item is ever stored in: the move that discards it. It is in the table so that discarding
// an item obeys the table like every other move.
export const REMOVED = 'removed';

const { PENDING, APPROVED, FINISHING, BLOCKED, REFUSED, CONFIRMED, UNCONFIRMED } = STATE;
export const TRANSITIONS = Object.freeze({
  [PENDING]: [APPROVED, BLOCKED, REMOVED],
  [APPROVED]: [FINISHING, BLOCKED, REMOVED],
  // Only the request that is clicking ends a finish, so an item cannot be discarded from under it.
  [FINISHING]: [BLOCKED, REFUSED, CONFIRMED, UNCONFIRMED],
  [BLOCKED]: [REMOVED],
  [REFUSED]: [REMOVED],
  [CONFIRMED]: [REMOVED],
  [UNCONFIRMED]: [REMOVED],
});

export const mayMove = (from, to) => TRANSITIONS[from]?.includes(to) ?? false;
// Open: a person can still check, approve or finish it. Terminal: it has its result and can only be discarded.
// An item that is being finished is neither.
export const isOpen = state => state === PENDING || state === APPROVED;
export const isTerminal = state => TRANSITIONS[state]?.every(to => to === REMOVED) ?? false;

export const REASON = Object.freeze({
  // What a check of the reviewed control found.
  matches: 'Matches what was reviewed.',
  tabClosed: 'The tab was closed.',
  documentReplaced: 'The page was reloaded or navigated.',
  controlGone: 'The reviewed control is no longer on the page.',
  covered: 'Something else now sits under the reviewed click point.',
  noCapture: 'The browser could not capture the reviewed area.',
  pixelsChanged: 'The reviewed area changed after it was reviewed.',
  // What happened to a click.
  fenced: 'The click did not land on the reviewed control, so it was stopped.',
  noKnownSign: 'This kind of control has no known sign of its result.',
  neitherSign: 'The application showed neither success nor refusal. Check the page; nothing will be clicked again.',
  workerStopped: 'The inbox stopped while this click was in progress. Check the page; nothing will be clicked again.',
  lostTrack: message => `The inbox lost track of this click: ${message}`,
  // Why a request was refused.
  notPending: 'This item is not waiting for approval.',
  notApproved: 'This item is not approved, or it is already being finished.',
  reviewAgain: reason => `${reason} Review it again before approving.`,
  removeWhileFinishing: 'This item is being clicked right now. Wait for its result, then clear it.',
});
export const OUTCOME = Object.freeze({
  blocked: 'Click blocked',
  refused: 'App refused',
  notConfirmed: noun => `Click sent; ${noun} not confirmed`,
  maybeSent: noun => `Click may have been sent; ${noun} not confirmed`,
});

// Why an item in state `from` may not move to `to`, in words for the person; null when it may.
export function refusal(from, to) {
  if (mayMove(from, to)) return null;
  if (to === APPROVED) return REASON.notPending;
  if (to === FINISHING) return REASON.notApproved;
  if (to === REMOVED && from === FINISHING) return REASON.removeWhileFinishing;
  return `An item that is ${from} cannot become ${to}.`;
}

// What an item becomes when the thing it is bound to goes away. An open item closes as blocked, with the
// reason. Anything else is left alone (null): a terminal item already has its result, and an item that is
// being finished belongs to that finish, which alone knows whether the click went out.
const closed = (item, reason, at) => isOpen(item.state)
  ? { state: BLOCKED, check: { at, holds: false, reason }, outcome: { text: OUTCOME.blocked, detail: reason, at } } : null;
export const whenTabCloses = (item, at) => closed(item, REASON.tabClosed, at);
// `documentId` is the document the item's tab shows now.
export const whenDocumentIs = (item, documentId, at) => documentId === item.documentId ? null : closed(item, REASON.documentReplaced, at);
