// A deliberately busy accounts-payable page for the review inbox. A clock, a
// rotating banner and a growing activity feed keep the compositor drawing new
// frames; the invoice under review can also be revised by someone else while a
// reviewer is looking. The application owns sign-in, invoice version and
// duplicate suppression. It logs every trusted click it receives separately
// from every commit it accepts.
import { randomUUID } from 'node:crypto';
import { createServer } from 'node:http';
import { once } from 'node:events';

const money = cents => '$' + (cents / 100).toLocaleString('en-US', { minimumFractionDigits: 2 });

export async function openApprovalsSite() {
  const accounts = new Map([[randomUUID(), 'payables']]);
  const invoices = new Map();
  const inputs = [], attempts = [], commits = [], receipts = new Map();
  const invoice = id => {
    if (!invoices.has(id)) invoices.set(id, { id, payee: 'Acme Tools', cents: 128000, revision: 1 });
    return invoices.get(id);
  };
  const server = createServer(async (req, res) => {
    const url = new URL(req.url, 'http://localhost'), path = url.pathname.split('/');
    res.setHeader('cache-control', 'no-store');
    const reply = (status, body) => { res.writeHead(status, { 'content-type': 'application/json' }); res.end(JSON.stringify(body)); };
    const json = async () => {
      const chunks = [];
      for await (const chunk of req) chunks.push(chunk);
      return JSON.parse(Buffer.concat(chunks).toString('utf8'));
    };
    if (req.method === 'GET' && path[1] === 'login' && accounts.has(path[2])) {
      res.writeHead(303, { location: '/approvals/' + (path[3] ?? 'INV-1042'),
        'set-cookie': 'payables=' + path[2] + '; HttpOnly; SameSite=Strict; Path=/' });
      res.end(); return;
    }
    const account = accounts.get(req.headers.cookie?.split('; ').find(item => item.startsWith('payables='))?.slice(9));
    if (!account) return reply(401, { error: 'sign-in-required' });
    if (req.method === 'GET' && path[1] === 'state') return reply(200, invoice(path[2]));
    if (req.method === 'POST' && path[1] === 'input') { inputs.push(await json()); return reply(200, {}); }
    if (req.method === 'GET' && path[1] === 'approvals') {
      const current = invoice(path[2]);
      res.setHeader('content-type', 'text/html; charset=utf-8');
      res.end(`<!doctype html><title>Payables · ${current.id}</title>
<style>
body{margin:0;font:14px/1.3 sans-serif;background:#f4f1ea;color:#1d1d1b}
#ticker{position:absolute;left:16px;top:8px;font-variant-numeric:tabular-nums}
#banner{position:absolute;left:420px;top:8px;width:40px;height:40px;background:#c2410c;animation:spin 1.2s linear infinite}
#pulse{position:absolute;left:480px;top:8px;width:40px;height:40px;animation:pulse .9s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}@keyframes pulse{50%{background:#0a7}to{background:#555}}
#approve{position:absolute;left:16px;top:72px;display:block;isolation:isolate;cursor:pointer}
#feed{position:absolute;left:16px;top:160px;margin:0;padding-left:18px;width:500px}
</style>
<div id="ticker"></div><div id="banner"></div><div id="pulse"></div>
<canvas id="approve" role="button" tabindex="0" aria-label="Approve payment" width="320" height="64"></canvas>
<ol id="feed"></ol>
<script>
const invoiceId = ${JSON.stringify(current.id)};
let state = ${JSON.stringify(current)};
// The approve control is drawn, as in canvas-rendered applications. What it
// shows is what a click on it commits.
const approve = document.getElementById('approve'), ink = approve.getContext('2d'), feed = document.getElementById('feed');
const money = cents => '$' + (cents / 100).toLocaleString('en-US', {minimumFractionDigits: 2});
const summary = () => state.payee + ' · ' + money(state.cents) + ' · ' + state.id;
const show = () => {
  ink.fillStyle = '#fff'; ink.fillRect(0, 0, 320, 64);
  ink.strokeStyle = '#1d1d1b'; ink.lineWidth = 2; ink.strokeRect(1, 1, 318, 62);
  ink.fillStyle = '#1d1d1b'; ink.font = 'bold 15px sans-serif'; ink.fillText('Approve payment', 12, 26);
  ink.font = '14px sans-serif'; ink.fillText(summary(), 12, 48);
  approve.dataset.summary = summary();
};
show();
// Unrelated motion: a clock, two animations above, and a feed that keeps growing below.
setInterval(() => { document.getElementById('ticker').textContent = 'Payables · ' + new Date().toISOString().slice(11, 23); }, 50);
let rows = 0;
setInterval(() => { const li = document.createElement('li'); li.textContent = 'Batch ' + (++rows) + ' reconciled'; feed.append(li);
  if (feed.children.length > 12) feed.firstChild.remove(); }, 250);
// Someone else may revise the invoice while it is under review.
setInterval(async () => { const next = await (await fetch('/state/' + invoiceId)).json();
  if (next.revision !== state.revision) { state = next; show(); } }, 100);
const key = crypto.randomUUID();
document.addEventListener('click', event => {
  if (!event.isTrusted) return;
  fetch('/input', { method: 'POST', keepalive: true, body: JSON.stringify({ target: event.target.closest('[id]')?.id ?? event.target.nodeName,
    shown: summary(), revision: state.revision }) });
}, true);
approve.onclick = async event => {
  if (!event.isTrusted) return;
  const response = await fetch('/commit/' + invoiceId, { method: 'POST',
    body: JSON.stringify({ invoice: invoiceId, cents: state.cents, payee: state.payee, revision: state.revision, key }) });
  window.lastReply = { status: response.status, body: await response.json() };
  if (response.ok) { const ack = document.createElement('span'); ack.id = 'receipt'; ack.textContent = 'Approved ' + window.lastReply.body.commitId;
    approve.replaceChildren(ack); }
};
</script>`);
      return;
    }
    if (req.method !== 'POST' || path[1] !== 'commit') return reply(404, {});
    let input;
    try { input = await json(); } catch { return reply(400, { error: 'invalid-request' }); }
    const current = invoice(path[2]);
    attempts.push({ account, invoice: path[2], cents: input?.cents, revision: input?.revision });
    if (!input || input.invoice !== path[2] || typeof input.key !== 'string' || !input.key) return reply(403, { error: 'wrong-invoice' });
    const prior = receipts.get(input.key);
    if (prior) return reply(200, prior);
    if (input.revision !== current.revision || input.cents !== current.cents) return reply(409, { error: 'stale-invoice' });
    const receipt = { commitId: randomUUID(), account, invoice: current.id, payee: current.payee, cents: current.cents, revision: current.revision };
    current.revision++;
    receipts.set(input.key, receipt);
    commits.push(receipt);
    return reply(200, receipt);
  });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const origin = 'http://127.0.0.1:' + server.address().port;
  return { origin, inputs, attempts, commits, money,
    login: (invoiceId = 'INV-1042') => `${origin}/login/${[...accounts.keys()][0]}/${invoiceId}`,
    // Another person corrects the invoice; open pages pick it up within a moment.
    revise(invoiceId, cents) { const current = invoice(invoiceId); current.cents = cents; current.revision++; return { ...current }; },
    async close() { const closed = once(server, 'close'); server.close(); server.closeAllConnections(); await closed; } };
}
