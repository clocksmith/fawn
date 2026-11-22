// Review inbox for one signed-in person. Every status shown here comes from
// the browser's own check of the reviewed pixels, never from the page.
const $ = selector => document.querySelector(selector);
const POLL_MS = 1500;
let people = [], me = null, busy = false;
const drafts = new Map();

function notice(text) { $('#notice').textContent = text ?? ''; $('#notice').hidden = !text; }
function node(tag, text, className) {
  const element = document.createElement(tag);
  if (text != null) element.textContent = text;
  if (className) element.className = className;
  return element;
}
async function api(method, body) {
  const response = await fetch('/api/inbox', { method, ...(body ? { headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) } : {}) });
  const data = await response.json();
  if (!response.ok) throw Object.assign(Error(data.error), { status: response.status });
  return data;
}
function button(text, action, className) {
  const element = node('button', text, className);
  element.type = 'button';
  element.addEventListener('click', async () => {
    if (busy) return;
    busy = true;
    try { notice(''); await action(); } catch (error) { notice(error.message); }
    busy = false;
    await refresh();
  });
  return element;
}
// The reviewed pixels, exactly as the browser captured them.
function image(review, label) {
  const figure = node('figure', null, 'review-image');
  const canvas = node('canvas');
  canvas.width = review.width; canvas.height = review.height;
  const bytes = Uint8ClampedArray.from(atob(review.rgba), c => c.charCodeAt(0));
  canvas.getContext('2d').putImageData(new ImageData(bytes, review.width, review.height), 0, 0);
  figure.append(canvas, node('figcaption', label));
  return figure;
}
function card(item) {
  const element = node('article', null, `panel inbox-item ${item.status}`);
  element.dataset.item = item.itemId;
  const heading = node('div', null, 'section-heading');
  heading.append(node('h2', item.title));
  const label = { 'still-holds': 'Still holds', 'needs-fresh-look': 'Needs a fresh look', finished: 'Finished', declined: 'Declined' }[item.status];
  const pill = node('span', label, `pill status-${item.status}`);
  pill.dataset.status = item.status;
  heading.append(pill);
  element.append(heading);
  if (item.change) element.append(node('p', item.change, 'lede'));
  if (item.statusReason) element.append(node('p', item.statusReason, 'attention'));
  const images = node('div', null, 'review-images');
  if (item.earlierReview && item.earlierReview.digest !== item.review.digest) images.append(image(item.earlierReview, 'Earlier image'));
  images.append(image(item.review, `Captured by ${item.review.capturedBy} at ${new Date(item.review.capturedAt).toLocaleTimeString()}`));
  element.append(images);
  const facts = node('ul', null, 'activity');
  facts.append(node('li', `Proposed by ${item.proposedBy}${item.assignee ? `, with ${item.assignee.name} now` : ''}`));
  for (const approval of item.approvals) {
    facts.append(node('li', approval.appliesToCurrentPixels ? `${approval.by} approved this exact image`
      : `${approval.by} approved an earlier image, which no longer counts`));
  }
  for (const handoff of item.handoffs) facts.append(node('li', `${handoff.from} handed off to ${handoff.to}${handoff.note ? `: “${handoff.note}”` : ''}`));
  for (const attempt of item.attempts) {
    facts.append(node('li', attempt.delivered ? 'The click was delivered to the reviewed target'
      : `The browser refused the click (${attempt.outcomeBasis}); nothing reached the page`));
  }
  element.append(facts);
  if (item.state !== 'open') return element;
  const row = node('div', null, 'button-row');
  const holds = item.status === 'still-holds';
  const mine = item.approvals.some(a => a.appliesToCurrentPixels && a.personId === me.personId);
  if (item.canReview && holds && !mine) row.append(button('Approve exactly this', () => api('POST', { op: 'approve', itemId: item.itemId, reviewId: item.review.reviewId }), 'primary'));
  if (item.canFinish && holds && item.approvedForCurrentPixels) row.append(button('Finish: act on this image', () => api('POST', { op: 'finish', itemId: item.itemId }), 'primary'));
  if (item.canReview) row.append(button(holds ? 'Look again' : 'Take a fresh look', () => api('POST', { op: 'lookAgain', itemId: item.itemId })));
  if (item.canReview) row.append(button('Decline', () => api('POST', { op: 'decline', itemId: item.itemId })));
  element.append(row);
  const others = people.filter(person => person.personId !== me.personId);
  if (others.length) {
    const draft = drafts.get(item.itemId) ?? { to: others[0].personId, note: '' };
    drafts.set(item.itemId, draft);
    const form = node('div', null, 'inline-form');
    const select = node('select'); select.setAttribute('aria-label', 'Hand off to');
    for (const person of others) { const option = node('option', person.name); option.value = person.personId; select.append(option); }
    select.value = draft.to;
    select.addEventListener('change', () => { draft.to = select.value; });
    const note = node('input'); note.placeholder = 'Note for the next person'; note.setAttribute('aria-label', 'Handoff note'); note.value = draft.note;
    note.addEventListener('input', () => { draft.note = note.value; });
    form.append(select, note, button('Hand off', async () => {
      await api('POST', { op: 'handoff', itemId: item.itemId, to: draft.to, note: draft.note });
      drafts.delete(item.itemId);
    }));
    element.append(form);
  }
  return element;
}
let rendered = '';
async function refresh() {
  let data;
  try { data = await api('GET'); }
  catch (error) { if (error.status === 401) { $('#signed-out').hidden = false; $('#inbox').hidden = true; return; } throw error; }
  me = data.person; people = data.people;
  $('#signed-out').hidden = true; $('#inbox').hidden = false;
  $('#who').textContent = `${me.name} · ${me.permissions.join(' + ')}`;
  // Leave the page alone while nothing changed, so a half-typed note survives.
  const identity = JSON.stringify(data.items.map(({ review, earlierReview, ...rest }) => [rest, review.digest]));
  if (identity === rendered) return;
  rendered = identity;
  const open = data.items.filter(item => item.state === 'open'), closed = data.items.filter(item => item.state !== 'open');
  $('#count').textContent = open.length ? `${open.length} thing${open.length === 1 ? '' : 's'} to look at` : 'Nothing needs you right now';
  $('#items').replaceChildren(...open.map(card));
  $('#closed-heading').hidden = !closed.length;
  $('#closed').replaceChildren(...closed.map(card));
}
async function start() {
  const token = location.hash.slice(1);
  if (token) {
    history.replaceState(null, '', '/inbox');
    const response = await fetch('/api/inbox/session', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ token }) });
    if (!response.ok) notice((await response.json()).error);
  }
  for (;;) {
    if (!busy) await refresh().catch(error => notice(error.message));
    await new Promise(resolve => setTimeout(resolve, POLL_MS));
  }
}
start();
