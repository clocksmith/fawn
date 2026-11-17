// When a conclusion names code, say where that code also occurs in saved sources the agent has not
// been shown. A long saved file is shown from its start only; an agent that concludes "this waits
// with no deadline" from one file may never open the part of another saved file that sets one.
// Pure: it reads only the saved records and the activity that says which slices were shown.
export const SHOWN_AT_FIRST_CHARS = 6000;
const MAX_TERMS = 12, MAX_MENTIONS = 5, MIN_TERM_CHARS = 6;
const FILE_SUFFIX = /\.(?:js|mjs|cjs|ts|tsx|jsx|json|md|py|zig|c|h|css|html|yaml|yml|toml)$/i;
const escape = text => text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

/** Identifiers in prose that look like code: camelCase, snake_case, or the parts of a dotted path. */
export function codeTerms(text) {
  const terms = new Set();
  for (const [match] of String(text ?? '').matchAll(/[A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*/g)) {
    for (const part of match.replace(FILE_SUFFIX, '').split('.')) {
      if (part.length >= MIN_TERM_CHARS && (/[a-z][A-Z]/.test(part) || /[A-Za-z]_[A-Za-z]/.test(part))) terms.add(part);
    }
  }
  return [...terms].sort((a, b) => b.length - a.length).slice(0, MAX_TERMS);
}

/** The parts of each saved source this investigation's agents were shown: its opening, plus every slice read. */
export function shownRanges(project) {
  const ranges = new Map();
  for (const artifact of Object.values(project.artifacts)) {
    if (artifact.kind === 'evidence' && typeof artifact.source?.content === 'string') ranges.set(artifact.id, [[0, SHOWN_AT_FIRST_CHARS]]);
  }
  for (const event of project.activity) {
    const output = event.event === 'tool-result' ? event.output : null;
    if (!output || output.field !== 'source' || !Number.isSafeInteger(output.offset) || !Number.isSafeInteger(output.end)) continue;
    // A slice of an earlier revision says nothing about the bytes saved now.
    if (ranges.has(output.artifactId) && project.artifacts[output.artifactId].revision === output.revision) ranges.get(output.artifactId).push([output.offset, output.end]);
  }
  return ranges;
}

/**
 * Where terms from `text` occur in saved repository sources outside what was shown. One place per
 * term and file, preferring a place that defines or assigns the term; bounded.
 *
 * @returns null, or { note, mentions: [{ term, artifactId, revision, path, offset, defines }] }
 */
export function unreadMentions(project, text) {
  const terms = codeTerms(text), ranges = shownRanges(project), found = [];
  const shown = (id, at) => ranges.get(id).some(([from, to]) => at >= from && at < to);
  for (const term of terms) {
    const occurrence = new RegExp(`(?<![\\w$])${escape(term)}(?![\\w$])`, 'g');
    const defining = new RegExp(`^(?:\\s*(?:=(?!=)|:(?!:)|\\([^)]*\\)\\s*\\{))`);
    for (const artifact of Object.values(project.artifacts)) {
      if (!ranges.has(artifact.id) || artifact.source.kind !== 'repository') continue;
      const content = artifact.source.content;
      let first = null, definition = null;
      for (const match of content.matchAll(occurrence)) {
        if (shown(artifact.id, match.index)) continue;
        first ??= match.index;
        const before = content.slice(Math.max(0, match.index - 24), match.index);
        if (defining.test(content.slice(match.index + term.length, match.index + term.length + 80)) || /(?:function\*?|class)\s+$/.test(before)) { definition = match.index; break; }
      }
      if (first !== null) found.push({ term, artifactId: artifact.id, revision: artifact.revision, path: artifact.source.path, offset: definition ?? first, defines: definition !== null });
    }
  }
  if (!found.length) return null;
  const mentions = found.sort((a, b) => Number(b.defines) - Number(a.defines)).slice(0, MAX_MENTIONS);
  return { note: 'Code named in this record also occurs in parts of saved sources that were not shown to you. Before relying on a claim that something is absent, unbounded or never happens, '
    + 'read those places with artifact_read (artifactId, expectedRevision, offset), then revise this record if it no longer holds.', mentions,
    ...(found.length > mentions.length ? { more: found.length - mentions.length } : {}) };
}
