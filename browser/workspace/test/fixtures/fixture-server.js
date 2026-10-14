// Fawn workspace tests — frozen fixture site with an independent external-effect oracle.
//
// Serves a research corpus, a contact form whose submissions the server records itself, and an
// export page. The server's receipts, not service logs, establish whether a submission happened.

import { createHash } from 'node:crypto';
import { createServer } from 'node:http';

const LOOPBACK = '127.0.0.1';
const SEE_OTHER = 303;
const UNPROCESSABLE = 422;

const DOCUMENTS = Object.freeze({
  'vulkan-memory': {
    title: 'Vulkan memory model',
    body: ['Vulkan memory model availability and visibility operations order writes.',
      'Unrelated paragraph about layout.'],
  },
  'vulkan-sync': {
    title: 'Vulkan synchronization',
    body: ['Pipeline barriers express Vulkan synchronization scopes.', 'Another unrelated paragraph.'],
  },
  bread: {
    title: 'Sourdough bread',
    body: ['Hydration and fermentation time shape the crumb.'],
  },
});

const REPORT_BYTES = Buffer.from('metric,value\nresearch_documents,2\nsubmissions,1\n');

function page(title, body) {
  return `<!doctype html><html><head><title>${title}</title></head><body>${body}</body></html>`;
}

function escapeHtml(text) {
  return String(text).replace(/[&<>"]/g, (char) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[char]);
}

const FORM_PAGE = page('Contact', `
  <h1>Contact</h1>
  <form method="post" action="/form/submit">
    <label>Name <input id="name" name="name"></label>
    <label>Email <input id="email" name="email"></label>
    <input type="hidden" name="idempotencyKey" value="">
    <button id="send" type="submit">Send</button>
  </form>`);

/**
 * Start the fixture server. `onSubmission` runs after a receipt is recorded and before the
 * response is sent; returning a promise holds the response.
 */
function startFixtureServer({ onSubmission = async () => {} } = {}) {
  const receipts = [];
  const server = createServer(async (request, response) => {
    const url = new URL(request.url, `http://${request.headers.host}`);
    const send = (status, body, headers = {}) => {
      response.writeHead(status, { 'content-type': 'text/html; charset=utf-8', ...headers });
      response.end(body);
    };
    if (url.pathname === '/research/') {
      const links = Object.entries(DOCUMENTS)
        .map(([slug, document]) => `<li><a href="/research/${slug}.html">${document.title}</a></li>`).join('');
      return send(200, page('Research index', `<h1>Research index</h1><ul>${links}</ul>`));
    }
    const documentMatch = /^\/research\/([a-z-]+)\.html$/.exec(url.pathname);
    if (documentMatch && DOCUMENTS[documentMatch[1]]) {
      const document = DOCUMENTS[documentMatch[1]];
      return send(200, page(document.title,
        `<h1>${document.title}</h1>${document.body.map((paragraph) => `<p>${paragraph}</p>`).join('')}`));
    }
    if (url.pathname === '/form') return send(200, FORM_PAGE);
    if (url.pathname === '/form/submit' && request.method === 'POST') {
      let raw = '';
      for await (const chunk of request) raw += chunk;
      const fields = Object.fromEntries(new URLSearchParams(raw));
      if (!fields.email?.includes('@')) {
        return send(UNPROCESSABLE, page('Rejected', '<p class="error" id="error">Email is invalid</p>'));
      }
      const duplicateOf = fields.idempotencyKey
        ? receipts.find((receipt) => receipt.fields.idempotencyKey === fields.idempotencyKey)
        : null;
      const receipt = duplicateOf ?? { id: receipts.length + 1, fields, receivedAt: new Date().toISOString() };
      if (!duplicateOf) receipts.push(receipt);
      await onSubmission(receipt);
      return send(SEE_OTHER, '', { location: `/form/confirmation?id=${receipt.id}` });
    }
    if (url.pathname === '/form/confirmation') {
      return send(200, page('Received', `<p id="receipt">Submission ${escapeHtml(url.searchParams.get('id'))} received</p>`));
    }
    if (url.pathname === '/submissions') {
      const items = receipts.map((receipt) => `<li id="submission-${receipt.id}">${escapeHtml(receipt.fields.name)} `
        + `${escapeHtml(receipt.fields.email)}</li>`).join('');
      return send(200, page('Submissions', `<h1>Submissions</h1><ul id="submissions">${items}</ul>`));
    }
    if (url.pathname === '/export') {
      return send(200, page('Export', '<h1>Export</h1><a id="report" href="/export/report.csv">Download report</a>'));
    }
    if (url.pathname === '/export/report.csv') {
      response.writeHead(200, {
        'content-type': 'text/csv',
        'content-disposition': 'attachment; filename="report.csv"',
      });
      return response.end(REPORT_BYTES);
    }
    return send(404, page('Not found', '<p>Not found</p>'));
  });
  return new Promise((resolve) => {
    server.listen(0, LOOPBACK, () => {
      const origin = `http://${LOOPBACK}:${server.address().port}`;
      resolve({
        origin,
        receipts,
        reportSha256: createHash('sha256').update(REPORT_BYTES).digest('hex'),
        close: () => new Promise((done) => {
          server.closeAllConnections();
          server.close(done);
        }),
      });
    });
  });
}

export { startFixtureServer };
