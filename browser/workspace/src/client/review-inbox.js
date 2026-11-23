// A review inbox: the few things an agent or a person needs someone to look at,
// each bound to the exact pixels the browser captured for it. An approval covers
// one browser review. Whoever picks an item up next sees, from the browser
// itself, whether that review still holds or needs a fresh look, and can act on
// exactly what the earlier reviewer saw only while it holds.
import { createHash, randomBytes, randomUUID, timingSafeEqual } from 'node:crypto';
import { join } from 'node:path';
import { PrivateJsonFile } from './private-json-file.js';

const PERMISSIONS = new Set(['review', 'finish']);
const MAX_REVIEW_BYTES = 4 * 512 * 512;
const digest = value => createHash('sha256').update(value).digest('hex');
const sameToken = (a, b) => a.length === b.length && timingSafeEqual(Buffer.from(a), Buffer.from(b));

// Plain-language reasons for a review that no longer holds.
const FRESH_LOOK = [
  [/pixels-changed|stale-compositor-revision/, 'The reviewed area changed after it was reviewed.'],
  [/target-changed|recipient/, 'Something else now sits under the reviewed click point.'],
  [/document-changed|stale-document|navigation/, 'The page was reloaded or navigated.'],
  [/review-expired|review-spent|unknown-review|superseded|ended|grant/, 'The browser no longer holds this review.'],
  [/surface|frame|hidden|viewport|scaled|emulation|child-frames|unverifiable/, 'The browser cannot verify this area right now.'],
];
export function freshLookReason(reason) {
  return FRESH_LOOK.find(([pattern]) => pattern.test(reason))?.[1] ?? `The browser reports: ${reason}.`;
}

export class ReviewInbox {
  constructor({ controller, stateDir }) {
    this.controller = controller;
    this.file = new PrivateJsonFile(join(stateDir, 'review-inbox.json'), { maxBytes: 64 * 1024 * 1024, lockWaitMs: 2000, lockRetryMs: 10 });
    this.state = this.file.read({ people: {}, items: {} });
    this.sessions = new Map();
  }
  save() { this.file.write(this.state); }

  // People. The operator invites each person with their own permissions; a
  // person signs in with their own link and acts only as themselves.
  invite({ name, permissions = ['review'] }) {
    if (typeof name !== 'string' || !name.trim() || name.length > 80) throw Error('Name the person you are inviting');
    if (!Array.isArray(permissions) || !permissions.length || permissions.some(p => !PERMISSIONS.has(p))) {
      throw Error('Permissions are review and finish');
    }
    const personId = randomUUID(), token = randomBytes(32).toString('base64url');
    this.state.people[personId] = { personId, name: name.trim(), permissions: [...new Set(permissions)], tokenHash: digest(token) };
    this.save();
    return { personId, name: name.trim(), permissions, token };
  }
  signIn(token) {
    if (typeof token !== 'string') throw Error('Open your invitation link');
    const hash = digest(token);
    const person = Object.values(this.state.people).find(p => sameToken(p.tokenHash, hash));
    if (!person) throw Error('Open your invitation link');
    const session = randomBytes(32).toString('base64url');
    this.sessions.set(session, person.personId);
    return { session, person: this.publicPerson(person) };
  }
  personFor(session) {
    const person = this.state.people[this.sessions.get(session)];
    if (!person) throw Object.assign(Error('Open your invitation link'), { status: 401 });
    return person;
  }
  publicPerson({ personId, name, permissions }) { return { personId, name, permissions }; }
  people() { return Object.values(this.state.people).map(p => this.publicPerson(p)); }
  #require(person, permission) {
    if (!person.permissions.includes(permission)) throw Error(`${person.name} does not have ${permission} permission`);
  }
  #item(itemId) {
    const item = this.state.items[itemId];
    if (!item) throw Error('Unknown review item');
    return item;
  }
  #event(item, event) { item.events.push({ at: new Date().toISOString(), ...event }); }

  async #capture(item, by) {
    const capture = await this.controller.reviewClickTarget(item.projectId, { pageId: item.pageId, selector: item.selector,
      ...(item.visibleRectangle ? { visibleRectangle: item.visibleRectangle } : {}), reviewSlot: item.slot,
      ...(item.completionRegions?.length ? { completionRegions: item.completionRegions } : {}) });
    if (Buffer.byteLength(capture.rgba ?? '', 'base64') > MAX_REVIEW_BYTES) throw Error('Select a smaller region to review');
    const review = { reviewId: capture.id, observationVersion: capture.observationVersion, handle: capture.handle, submitsForm: capture.submitsForm === true,
      digest: capture.baselineDigest, frameRevision: capture.frameRevision, width: capture.pixelWidth, height: capture.pixelHeight,
      rgba: capture.rgba, capturedBy: by, capturedAt: new Date().toISOString() };
    item.reviews.push(review);
    this.#event(item, { kind: 'captured', by, reviewId: review.reviewId, digest: review.digest });
    return review;
  }

  // An agent or person proposes one change for someone to look at. The browser
  // captures the exact region now; that capture is what reviewers approve.
  async propose(projectId, { pageId = 'main', selector, visibleRectangle, title, change, assignee = null, proposedBy = 'agent', completion, completionRegions = [] }) {
    if (typeof title !== 'string' || !title.trim()) throw Error('Say what needs a look');
    if (assignee && !this.state.people[assignee]) throw Error('Unknown assignee');
    const itemId = randomUUID();
    const item = { itemId, slot: `inbox-${itemId.slice(0, 18)}`, projectId, pageId, selector, visibleRectangle: visibleRectangle ?? null,
      title: title.trim(), change: typeof change === 'string' ? change.trim() : '', proposedBy, assignee,
      completion: completion ?? null, completionRegions, state: 'open', reviews: [], approvals: [], handoffs: [], attempts: [], events: [] };
    await this.#capture(item, proposedBy);
    this.state.items[itemId] = item;
    this.#event(item, { kind: 'proposed', by: proposedBy, assignee });
    this.save();
    return this.view(item, await this.#status(item));
  }

  async #status(item) {
    if (item.state !== 'open') return { holds: false, reason: item.state };
    const review = item.reviews.at(-1);
    try {
      return await this.controller.service.reviewedClickStatus({ ...this.controller.admin(item.projectId),
        pageId: item.pageId, reviewId: review.reviewId });
    } catch (error) { return { holds: false, reason: error.code ?? error.message }; }
  }

  view(item, status, viewer = null) {
    const current = item.reviews.at(-1);
    const name = id => this.state.people[id]?.name ?? id;
    const approvals = item.approvals.map(a => ({ by: name(a.personId), personId: a.personId, at: a.at,
      appliesToCurrentPixels: a.reviewId === current.reviewId }));
    const earlier = item.reviews.length > 1 ? item.reviews.at(-2) : null;
    return {
      itemId: item.itemId, title: item.title, change: item.change, pageId: item.pageId, selector: item.selector,
      // What the workspace will read after the click to learn the result.
      readsAfterClick: item.completionRegions ?? [],
      proposedBy: item.proposedBy === 'agent' ? 'agent' : name(item.proposedBy),
      assignee: item.assignee ? { personId: item.assignee, name: name(item.assignee) } : null,
      state: item.state,
      status: item.state !== 'open' ? item.state : status.holds ? 'still-holds' : 'needs-fresh-look',
      statusReason: status.holds || item.state !== 'open' ? null : freshLookReason(status.reason),
      browserReason: status.reason,
      review: { reviewId: current.reviewId, digest: current.digest, width: current.width, height: current.height, rgba: current.rgba,
        capturedBy: current.capturedBy === 'agent' ? 'agent' : name(current.capturedBy), capturedAt: current.capturedAt },
      earlierReview: earlier && { digest: earlier.digest, width: earlier.width, height: earlier.height, rgba: earlier.rgba },
      approvals,
      approvedForCurrentPixels: approvals.some(a => a.appliesToCurrentPixels),
      handoffs: item.handoffs.map(h => ({ from: name(h.from), to: name(h.to), note: h.note, at: h.at })),
      attempts: item.attempts, events: item.events.map(e => ({ ...e, by: e.by && e.by !== 'agent' ? name(e.by) : e.by })),
      ...(viewer ? { canFinish: viewer.permissions.includes('finish'), canReview: viewer.permissions.includes('review') } : {}),
    };
  }

  // Only what needs this person: items assigned to them, or unassigned.
  async list(person = null) {
    const items = Object.values(this.state.items).filter(item => !person || item.assignee === person.personId
      || (!item.assignee && item.state === 'open') || item.approvals.some(a => a.personId === person.personId));
    const statuses = await Promise.all(items.map(item => this.#status(item)));
    return items.map((item, i) => this.view(item, statuses[i], person))
      .sort((a, b) => (a.state === 'open' ? 0 : 1) - (b.state === 'open' ? 0 : 1));
  }

  async approve(person, { itemId, reviewId }) {
    this.#require(person, 'review');
    const item = this.#item(itemId);
    if (item.state !== 'open') throw Error('This item is closed');
    const current = item.reviews.at(-1);
    if (reviewId !== current.reviewId) throw Error('You approved an earlier image. Look at the current one.');
    const status = await this.#status(item);
    if (!status.holds) throw Object.assign(Error(`${freshLookReason(status.reason)} Look again before approving.`), { code: 'needs-fresh-look' });
    if (!item.approvals.some(a => a.personId === person.personId && a.reviewId === reviewId)) {
      item.approvals.push({ personId: person.personId, reviewId, digest: current.digest, at: new Date().toISOString() });
    }
    this.#event(item, { kind: 'approved', by: person.personId, reviewId, digest: current.digest });
    this.save();
    return this.view(item, status, person);
  }

  async handoff(person, { itemId, to, note = '' }) {
    const item = this.#item(itemId);
    if (item.state !== 'open') throw Error('This item is closed');
    if (!this.state.people[to]) throw Error('Hand off to an invited person');
    if (item.assignee && item.assignee !== person.personId && !person.permissions.includes('review')) throw Error('This item is assigned to someone else');
    item.handoffs.push({ from: person.personId, to, note: String(note).slice(0, 2000), at: new Date().toISOString(),
      reviewId: item.reviews.at(-1).reviewId });
    item.assignee = to;
    this.#event(item, { kind: 'handed-off', by: person.personId, to });
    this.save();
    return this.view(item, await this.#status(item), person);
  }

  // A fresh capture replaces the item's browser review. Earlier approvals stay in
  // the history but no longer cover what the browser now shows.
  async lookAgain(person, { itemId }) {
    this.#require(person, 'review');
    const item = this.#item(itemId);
    if (item.state !== 'open') throw Error('This item is closed');
    await this.#capture(item, person.personId);
    this.save();
    return this.view(item, await this.#status(item), person);
  }

  // Act on exactly what an approver saw: the browser admits the click only while
  // that review still holds, and delivers it only to the reviewed target.
  async finish(person, { itemId }) {
    this.#require(person, 'finish');
    const item = this.#item(itemId);
    if (item.state !== 'open') throw Error('This item is closed');
    const current = item.reviews.at(-1);
    if (!item.approvals.some(a => a.reviewId === current.reviewId)) throw Error('No one has approved the current image yet');
    const actionId = `inbox-${item.itemId}-${item.attempts.length + 1}`;
    const result = await this.controller.clickReviewedTarget(item.projectId, { pageId: item.pageId,
      observationVersion: current.observationVersion, handle: current.handle, actionId, submitsForm: current.submitsForm === true,
      // Bound to the reviewed region, not to the newest page observation: other
      // items and the rest of the page move on while this one waits.
      observationScope: { kind: 'regions', selectors: [item.selector] },
      effect: { external: true, effectKey: actionId }, ...(item.completion ? { completion: item.completion } : {}) });
    const effect = result.evidence?.effect ?? {};
    const delivered = effect.delivered === true;
    const attempt = { actionId, by: person.personId, reviewId: current.reviewId, outcome: result.outcome ?? 'not-dispatched',
      outcomeBasis: result.outcomeBasis ?? result.neverDispatched?.code ?? null,
      admitted: effect.admitted ?? result.evidence?.inputAdmitted ?? false,
      delivered, at: new Date().toISOString() };
    item.attempts.push(attempt);
    if (delivered) item.state = 'finished';
    this.#event(item, { kind: delivered ? 'finished' : 'finish-refused', by: person.personId, outcomeBasis: result.outcomeBasis });
    this.save();
    const status = delivered ? { holds: false, reason: 'finished' } : { holds: false, reason: result.outcomeBasis ?? 'review-spent' };
    return { attempt, item: this.view(item, status, person) };
  }

  decline(person, { itemId, note = '' }) {
    this.#require(person, 'review');
    const item = this.#item(itemId);
    if (item.state !== 'open') throw Error('This item is closed');
    item.state = 'declined';
    this.#event(item, { kind: 'declined', by: person.personId, note: String(note).slice(0, 2000) });
    this.save();
    return this.view(item, { holds: false, reason: 'declined' }, person);
  }
}
