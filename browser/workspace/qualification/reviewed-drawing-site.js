// The application owns record identity, account authorization, version and
// duplicate suppression. All browser arms see precisely the same local site.
import { randomUUID } from 'node:crypto';
import { createServer } from 'node:http';
import { once } from 'node:events';

export async function openDrawingSite() {
  const accounts = new Map(['sender', 'recipient'].map(name => [randomUUID(), name]));
  const records = new Map(['repair', 'lookalike', 'visual-only', 'late-write'].map(name => [name, { revision: 1 }]));
  const receipts = new Map();
  const attempts = [];
  let pause = null;
  const form = (account, record, revision) => ({ account, record, revision, key: randomUUID() });
  const server = createServer(async (req, res) => {
    const path = new URL(req.url, 'http://localhost').pathname.split('/');
    res.setHeader('cache-control', 'no-store');
    function reply(status, body) {
      res.writeHead(status, { 'content-type': 'application/json' });
      res.end(JSON.stringify(body));
    }
    if (req.method === 'GET' && path[1] === 'login' && accounts.has(path[2])) {
      res.writeHead(303, { location: '/draw/repair',
        'set-cookie': 'review=' + path[2] + '; HttpOnly; SameSite=Strict; Path=/' });
      res.end(); return;
    }
    const credential = req.headers.cookie?.split('; ').find(item => item.startsWith('review='))?.slice(7);
    const account = accounts.get(credential);
    if (!account) return reply(401, { error: 'sign-in-required' });
    const record = records.get(path[2]);
    if (!record) return reply(404, { error: 'unknown-record' });
    if (req.method === 'GET' && path[1] === 'draw') {
      const request = form(account, path[2], record.revision);
      res.setHeader('content-type', 'text/html; charset=utf-8');
      res.end(`<!doctype html><title>Review drawing</title>
        <style>body{margin:0}canvas{display:block;isolation:isolate}</style>
        <canvas id="drawing" role="button" tabindex="0" aria-label="Repair drawing" width="48" height="48"></canvas>
        <script>
        const canvas = document.querySelector('canvas'), ctx = canvas.getContext('2d');
        ctx.fillStyle = '#c80000'; ctx.fillRect(0,0,48,48);
        const request = ${JSON.stringify(request)};
        canvas.onclick = async () => {
          const response = await fetch('/commit/' + request.record, {
            method: 'POST', headers: {'content-type':'application/json'}, body: JSON.stringify(request)
          });
          window.lastReply = {status:response.status, receipt:await response.json()};
        };
        </script>`);
      return;
    }
    if (req.method !== 'POST' || path[1] !== 'commit') return reply(404, {});
    let input;
    try {
      const chunks = [];
      for await (const chunk of req) chunks.push(chunk);
      input = JSON.parse(Buffer.concat(chunks).toString('utf8'));
    } catch { return reply(400, { error: 'invalid-request' }); }
    attempts.push({ account, record: path[2], revision: input?.revision });
    if (!input || account !== input.account || input.record !== path[2] || typeof input.key !== 'string'
        || !input.key || !Number.isSafeInteger(input.revision)) return reply(403, { error: 'wrong-record-or-account' });
    if (pause?.record === input.record) {
      const held = pause;
      pause = null;
      held.arrived.resolve();
      await held.resume.promise;
    }
    const identity = JSON.stringify([account, input.key]);
    const prior = receipts.get(identity);
    if (prior) {
      if (prior.record !== input.record || prior.revision !== input.revision) return reply(409, { error: 'key-conflict' });
      return reply(200, prior);
    }
    if (record.revision !== input.revision) return reply(409, { error: 'stale-record' });
    const receipt = { account, record: input.record, revision: record.revision, writeId: randomUUID() };
    record.revision++;
    receipts.set(identity, receipt);
    return reply(200, receipt);
  });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const origin = 'http://127.0.0.1:' + server.address().port;
  return { origin, attempts, receipts, revise: name => records.get(name).revision++,
    pauseCommit(record) {
      if (pause) throw Error('Already holding a fixture commit');
      const arrived = Promise.withResolvers(), resume = Promise.withResolvers();
      pause = { record, arrived, resume };
      return { arrived: arrived.promise, release: resume.resolve };
    },
    login: name => origin + '/login/' + [...accounts].find(([, value]) => value === name)[0],
    async close() { const closed = once(server, 'close'); server.close(); server.closeAllConnections(); await closed; } };
}
