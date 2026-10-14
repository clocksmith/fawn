// Fawn workspace — page query shared by the narrow browser adapters
//
// One in-page reader produces observations and element probes for every adapter, so element
// identity and content hashes mean the same thing whichever protocol drives Chromium.

import { canonicalJson, sha256Hex } from '../journal.js';

const SNAPSHOT_MODE = 'snapshot';
const ELEMENT_MODE = 'element';
const ELEMENT_TEXT_CHARS = 200;

// Runs inside the page; must stay self-contained.
function queryPage({ mode, selector, maxElements, maxTextChars, elementTextChars, regions = [] }) {
  const INTERACTIVE = 'a[href], button, input, select, textarea, [role="button"], [role="link"]';
  const isUnique = (candidate) => {
    try {
      return document.querySelectorAll(candidate).length === 1;
    } catch {
      return false;
    }
  };
  const pathSelector = (node) => {
    const parts = [];
    for (let current = node; current && current !== document.documentElement; current = current.parentElement) {
      const tag = current.tagName.toLowerCase();
      const siblings = current.parentElement
        ? [...current.parentElement.children].filter((child) => child.tagName === current.tagName)
        : [current];
      parts.unshift(siblings.length > 1 ? `${tag}:nth-of-type(${siblings.indexOf(current) + 1})` : tag);
    }
    return `html > ${parts.join(' > ')}`;
  };
  const stableSelector = (node) => {
    const tag = node.tagName.toLowerCase();
    const candidates = [];
    if (node.id) candidates.push(`#${CSS.escape(node.id)}`);
    const name = node.getAttribute('name');
    if (name) candidates.push(`${tag}[name="${CSS.escape(name)}"]`);
    const href = node.getAttribute('href');
    if (href) candidates.push(`${tag}[href="${CSS.escape(href)}"]`);
    return candidates.find(isUnique) ?? pathSelector(node);
  };
  // FNV-1a over the full text, so identity covers text beyond the displayed prefix.
  const fingerprint = (text) => {
    let hash = 0x811c9dc5;
    for (let index = 0; index < text.length; index += 1) {
      hash ^= text.charCodeAt(index);
      hash = Math.imul(hash, 0x01000193) >>> 0;
    }
    return `${text.length}:${hash.toString(16)}`;
  };
  const fullText = (node) => (node.innerText ?? node.textContent ?? '').trim();
  const describe = (node) => {
    const attributes = [...node.attributes]
      .map((attribute) => [attribute.name, attribute.value])
      .sort((a, b) => (a[0] < b[0] ? -1 : 1));
    const formAction = node.form && ['submit', 'image'].includes(node.type)
      ? (node.formAction || node.form.action)
      : null;
    const text = fullText(node);
    return {
      tag: node.tagName.toLowerCase(),
      attributes,
      text: text.slice(0, elementTextChars),
      textFingerprint: fingerprint(text),
      value: 'value' in node ? String(node.value) : null,
      href: node.href ?? null,
      formAction,
    };
  };
  if (mode === 'element') {
    const nodes = document.querySelectorAll(selector);
    if (nodes.length !== 1) return { count: nodes.length, element: null, fullText: null };
    return { count: 1, element: describe(nodes[0]), fullText: fullText(nodes[0]) };
  }
  const CONTROLS = 'input, select, textarea';
  const readRegion = (regionSelector) => {
    let matches;
    try {
      matches = document.querySelectorAll(regionSelector);
    } catch {
      return { selector: regionSelector, count: 0, html: null, values: null };
    }
    if (matches.length !== 1) return { selector: regionSelector, count: matches.length, html: null, values: null };
    const [region] = matches;
    const controls = [...(region.matches(CONTROLS) ? [region] : []), ...region.querySelectorAll(CONTROLS)];
    return { selector: regionSelector, count: 1, html: region.outerHTML, values: controls.map((control) => String(control.value)) };
  };
  const nodes = [...document.querySelectorAll(INTERACTIVE)];
  return {
    url: location.href,
    title: document.title,
    html: document.documentElement.outerHTML,
    text: (document.body?.innerText ?? '').slice(0, maxTextChars),
    isTruncated: nodes.length > maxElements,
    elements: nodes.slice(0, maxElements).map((node) => ({ selector: stableSelector(node), ...describe(node) })),
    regions: regions.map(readRegion),
  };
}

function elementDigest(element) {
  return sha256Hex(canonicalJson({
    tag: element.tag,
    attributes: element.attributes,
    text: element.text,
    textFingerprint: element.textFingerprint,
  }));
}

function snapshotArgs(config, regions = []) {
  return {
    mode: SNAPSHOT_MODE,
    maxElements: config.observation.maxElements,
    maxTextChars: config.observation.maxTextChars,
    elementTextChars: ELEMENT_TEXT_CHARS,
    regions,
  };
}

/** A region digest covers its markup and control values; a missing or ambiguous region has none. */
function regionDigest(region) {
  return region.count === 1 ? sha256Hex(canonicalJson({ html: region.html, values: region.values })) : null;
}

function elementArgs(selector) {
  return { mode: ELEMENT_MODE, selector, elementTextChars: ELEMENT_TEXT_CHARS };
}

/** Convert an in-page snapshot into the service's observation input. */
function buildSnapshot(raw, pageCount) {
  const elements = raw.elements.map((element) => ({ ...element, digest: elementDigest(element) }));
  const controlValues = elements.filter((element) => element.value !== null)
    .map((element) => [element.selector, element.value]);
  return {
    url: raw.url,
    title: raw.title,
    text: raw.text,
    isTruncated: raw.isTruncated,
    elements,
    pageCount,
    contentHash: sha256Hex(canonicalJson({ url: raw.url, html: raw.html, controlValues })),
    regionDigests: Object.fromEntries((raw.regions ?? []).map((region) => [region.selector, regionDigest(region)])),
  };
}

/**
 * Convert an in-page element probe into the service's probe result. Conditions read the full
 * text: a displayed prefix would make text-excludes report absence that is not established.
 */
function buildProbe(result) {
  if (!result.element) return { count: result.count, digest: null, text: null, value: null };
  return {
    count: result.count,
    digest: elementDigest(result.element),
    text: result.fullText,
    value: result.element.value,
  };
}

export { buildProbe, buildSnapshot, elementArgs, queryPage, snapshotArgs };
