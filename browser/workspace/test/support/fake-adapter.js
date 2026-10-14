// Fawn workspace tests — in-memory page model with an on-disk external receipt log
//
// Exercises service semantics without a browser. External submissions append to a receipts
// file outside the service state directory so restart tests have an independent oracle.

import { appendFileSync, existsSync, readFileSync } from 'node:fs';
import { canonicalJson, sha256Hex } from '../../src/journal.js';

const BLANK_URL = 'about:blank';
const ORIGIN = 'https://fixture.test';

const SITE = Object.freeze({
  [`${ORIGIN}/form`]: {
    title: 'Contact form',
    text: 'Contact form',
    elements: [
      { selector: '#name', tag: 'input', attributes: [['id', 'name'], ['name', 'name']], text: '', isInput: true },
      { selector: '#email', tag: 'input', attributes: [['id', 'email'], ['name', 'email']], text: '', isInput: true },
      {
        selector: '#send',
        tag: 'button',
        attributes: [['id', 'send'], ['type', 'submit']],
        text: 'Send',
        formAction: `${ORIGIN}/form/submit`,
        submitsTo: `${ORIGIN}/form/done`,
      },
      { selector: '#other', tag: 'a', attributes: [['href', '/other'], ['id', 'other']], text: 'Other', navigatesTo: `${ORIGIN}/other` },
    ],
  },
  [`${ORIGIN}/form/done`]: {
    title: 'Done',
    text: 'Submission received',
    elements: [{ selector: '#receipt', tag: 'p', attributes: [['id', 'receipt']], text: 'Submission received' }],
  },
  [`${ORIGIN}/other`]: {
    title: 'Other',
    text: 'Other page',
    elements: [{ selector: '#back', tag: 'a', attributes: [['href', '/form'], ['id', 'back']], text: 'Back', navigatesTo: `${ORIGIN}/form` }],
  },
  [`${ORIGIN}/export`]: {
    title: 'Export',
    text: 'Export',
    elements: [{ selector: '#report', tag: 'a', attributes: [['href', '/report.csv'], ['id', 'report']], text: 'Report', downloads: 'a,b\n1,2\n' }],
  },
});

function digestOf(element) {
  return sha256Hex(canonicalJson({ tag: element.tag, attributes: element.attributes, text: element.text }));
}

class FakeAdapter {
  constructor({ receiptsPath }) {
    this.receiptsPath = receiptsPath;
    this.pages = new Map();
    this.held = null;
    this.nextFailure = null;
    this.executions = [];
  }

  async openWorkspace(workspaceId) {
    if (!this.pages.has(workspaceId)) this.pages.set(workspaceId, { url: BLANK_URL, values: {}, mutation: 0 });
  }

  _page(workspaceId) {
    const page = this.pages.get(workspaceId);
    if (!page) throw new Error(`workspace ${workspaceId} has no open page`);
    return page;
  }

  _elements(page) {
    const site = SITE[page.url];
    if (!site) return [];
    return site.elements.map((element) => ({
      ...element,
      value: element.isInput ? (page.values[element.selector] ?? '') : null,
      href: element.navigatesTo ?? null,
      formAction: element.formAction ?? null,
      digest: digestOf(element),
    }));
  }

  async snapshot(workspaceId) {
    const page = this._page(workspaceId);
    const site = SITE[page.url] ?? { title: '', text: '' };
    return {
      url: page.url,
      title: site.title,
      text: site.text,
      isTruncated: false,
      elements: this._elements(page),
      pageCount: 1,
      contentHash: sha256Hex(canonicalJson({ url: page.url, values: page.values, mutation: page.mutation })),
    };
  }

  async probeUrl(workspaceId) {
    return this._page(workspaceId).url;
  }

  async probeElement(workspaceId, selector) {
    const matches = this._elements(this._page(workspaceId)).filter((element) => element.selector === selector);
    if (matches.length !== 1) return { count: matches.length, digest: null, text: null, value: null };
    const [element] = matches;
    return { count: 1, digest: element.digest, text: element.text, value: element.value };
  }

  /** Simulate human or page activity between observation and dispatch. */
  mutate(workspaceId) {
    this._page(workspaceId).mutation += 1;
  }

  /** Hold the next effect in flight until the returned release function is called. */
  holdNext() {
    let release;
    const gate = new Promise((resolve) => {
      release = resolve;
    });
    this.held = gate;
    return release;
  }

  failNext(message) {
    this.nextFailure = message;
  }

  receipts() {
    if (!existsSync(this.receiptsPath)) return [];
    return readFileSync(this.receiptsPath, 'utf8').trim().split('\n').filter(Boolean).map((line) => JSON.parse(line));
  }

  async execute(workspaceId, request) {
    const page = this._page(workspaceId);
    this.executions.push(request.actionId);
    if (this.held) {
      const gate = this.held;
      this.held = null;
      await gate;
    }
    if (this.nextFailure) {
      const message = this.nextFailure;
      this.nextFailure = null;
      throw new Error(message);
    }
    if (request.kind === 'navigate') {
      page.url = request.target.url;
      page.values = {};
      return { finalUrl: page.url };
    }
    const element = this._elements(page).find((candidate) => candidate.selector === request.target.selector);
    if (!element) throw new Error(`no element ${request.target.selector}`);
    if (request.kind === 'fill') {
      page.values[element.selector] = request.params.value;
      return { selector: element.selector };
    }
    if (request.kind === 'download') {
      return { byteLength: element.downloads.length, sha256: sha256Hex(element.downloads) };
    }
    if (element.submitsTo) {
      appendFileSync(this.receiptsPath, `${JSON.stringify({
        actionId: request.actionId,
        effectKey: request.effect?.effectKey ?? null,
        externalIdempotencyKey: request.effect?.externalIdempotencyKey ?? null,
        values: page.values,
      })}\n`);
      page.url = element.submitsTo;
      page.values = {};
      return { selector: element.selector };
    }
    if (element.navigatesTo) {
      page.url = element.navigatesTo;
      page.values = {};
    }
    return { selector: element.selector };
  }

  async close() {
    this.pages.clear();
  }
}

export { FakeAdapter, ORIGIN };
