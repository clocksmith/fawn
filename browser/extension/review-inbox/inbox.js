// The inbox page. It runs in the side panel, and the same page works in a tab.
// Each card answers four questions about one approval: what will change, what
// you saw, what changed since, and what actually happened.
const call = async message => {
  const reply = await chrome.runtime.sendMessage(message);
  if (!reply?.ok) throw new Error(reply?.error ?? 'The inbox did not answer.');
  return reply.value;
};
const find = selector => document.querySelector(selector);
const node = (tag, text, className) => {
  const element = document.createElement(tag);
  if (text != null) element.textContent = text;
  if (className) element.className = className;
  return element;
};
const notice = text => { const element = find('[data-notice]'); element.textContent = text ?? ''; element.hidden = !text; };
const PILLS = { pending: 'Waiting for approval', approved: 'Approved', finishing: 'Clicking…', blocked: 'Click blocked', refused: 'App refused',
  confirmed: 'Done', unconfirmed: 'Not confirmed' };
const time = iso => new Date(iso).toLocaleTimeString();
// The image is shown at the size it had on the page, whatever the screen's pixel density. One wider than
// the card scrolls sideways inside its own frame, and says so, so it is never shrunk until it cannot be read.
const picture = (png, caption, width) => {
  const figure = node('figure'), frame = node('div', null, 'shot'), image = node('img'), note = node('figcaption', caption);
  image.src = 'data:image/png;base64,' + png; image.alt = caption;
  if (width) image.width = Math.round(width);
  frame.tabIndex = 0;
  const mark = () => { note.textContent = frame.scrollWidth > frame.clientWidth + 1 ? caption + '. Scroll sideways to see all of it.' : caption; };
  image.addEventListener('load', mark); new ResizeObserver(mark).observe(frame);
  frame.append(image); figure.append(frame, note);
  return figure;
};
let busy = false;
// The inbox's own record is the truth, not the reply to a request: a finish that has settled is shown as
// soon as it is recorded, even if the reply is slow or lost while the browser switches tabs.
const settled = id => new Promise(resolve => {
  const look = async () => {
    const item = ((await chrome.storage.session.get('items')).items ?? {})[id];
    if (item && ['approved', 'finishing'].includes(item.state)) return;
    chrome.storage.session.onChanged.removeListener(look);
    resolve();
  };
  chrome.storage.session.onChanged.addListener(look);
});
async function act(button, message) {
  if (busy) return;
  busy = true; notice(null);
  for (const other of document.querySelectorAll('button')) other.disabled = true;
  try { await (message.inbox === 'finish' ? Promise.race([call(message), settled(message.id)]) : call(message)); }
  catch (error) { notice(error.message); }
  finally { busy = false; await render(); }
}

function card(item) {
  const element = node('article', null, 'card');
  element.dataset.item = item.id; element.dataset.state = item.state;
  const header = node('header'), heading = node('div');
  heading.append(node('h2', item.summary.title), node('p', item.pageTitle || item.url, 'where'));
  const pill = node('span', PILLS[item.state] ?? item.state, 'pill'); pill.dataset.pill = item.state;
  header.append(heading, pill);
  element.append(header);

  element.append(node('h3', 'What will change'));
  const list = node('ul');
  for (const line of item.summary.lines) list.append(node('li', line));
  element.append(list);

  element.append(node('h3', 'What you saw'));
  const seen = node('div', null, 'seen');
  seen.append(picture(item.image, `Captured at ${time(item.createdAt)}`, item.rect?.width));
  if (item.check?.image) seen.append(picture(item.check.image, `Now, at ${time(item.check.at)}`, item.rect?.width));
  element.append(seen);

  element.append(node('h3', 'What changed since'));
  const open = ['pending', 'approved'].includes(item.state);
  const status = node('p', item.check?.holds ? `Nothing. Checked at ${time(item.check.at)}.` : item.check?.reason ?? 'Not checked yet.',
    'status' + (item.check?.holds === false ? ' changed' : ''));
  status.dataset.check = item.check?.holds === false ? 'changed' : 'holds';
  element.append(status);

  element.append(node('h3', 'What actually happened'));
  const outcome = node('div', null, 'outcome ' + (item.outcome ? item.state : item.state === 'finishing' ? 'finishing' : 'none'));
  outcome.dataset.outcome = item.outcome ? item.state : '';
  if (item.outcome) {
    outcome.append(node('strong', item.outcome.text));
    if (item.outcome.detail) outcome.append(node('span', item.outcome.detail));
  } else outcome.textContent = item.state === 'finishing' ? 'Clicking the reviewed control…' : 'Nothing yet. Nothing is clicked until you finish.';
  element.append(outcome);

  const actions = node('div', null, 'actions');
  const button = (label, action, primary) => {
    const control = node('button', label, primary ? 'primary' : null);
    control.type = 'button'; control.dataset.action = action;
    control.addEventListener('click', () => act(control, { inbox: action, id: item.id }));
    actions.append(control);
  };
  if (item.state === 'pending' && item.check?.holds !== false) button('Approve exactly this', 'approve', true);
  if (item.state === 'approved' && item.check?.holds !== false) button('Finish: click it', 'finish', true);
  if (open) button('Check again', 'check');
  button(open ? 'Remove' : 'Clear', 'remove');
  element.append(actions);
  return element;
}

let drawn = '';
async function render() {
  const [items, tabs] = await Promise.all([call({ inbox: 'items' }), call({ inbox: 'tabs' })]);
  const select = find('[data-tabs]'), chosen = select.value;
  select.replaceChildren(...tabs.map(tab => { const option = node('option', tab.title || tab.url); option.value = String(tab.id); return option; }));
  const preferred = tabs.find(tab => String(tab.id) === chosen) ?? tabs.find(tab => tab.active) ?? tabs[0];
  if (preferred) select.value = String(preferred.id);
  find('[data-pick]').disabled = busy || !tabs.length;
  const signature = JSON.stringify(items.map(item => [item.id, item.state, item.check, item.outcome]));
  if (signature !== drawn) { drawn = signature; find('[data-items]').replaceChildren(...items.map(card)); }
  if (busy) for (const button of document.querySelectorAll('button')) button.disabled = true;
  find('[data-empty]').hidden = items.length > 0;
}

find('[data-pick]').addEventListener('click', event => {
  const tabId = Number(find('[data-tabs]').value);
  if (tabId) act(event.currentTarget, { inbox: 'pick', tabId });
});
chrome.storage.session.onChanged.addListener(() => render());
chrome.tabs.onUpdated.addListener(() => { if (!busy) render(); });
chrome.tabs.onRemoved.addListener(() => { if (!busy) render(); });
render();
