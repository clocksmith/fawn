// Read saved investigation bytes, never a live page or an arbitrary filesystem path.
import { sha256Hex } from '../journal.js';

export const MAX_ARTIFACT_READ_CHARS = 6000;

export function readArtifact(project, { artifactId, expectedRevision, offset = 0, maxChars = MAX_ARTIFACT_READ_CHARS }, visibleArtifacts) {
  if (!(visibleArtifacts instanceof Set) || !visibleArtifacts.has(artifactId)) throw Error('Source record was not shared with this task');
  const artifact = Object.hasOwn(project.artifacts, artifactId) ? project.artifacts[artifactId] : null;
  if (!artifact) throw Error('Unknown source record');
  if (!Number.isSafeInteger(expectedRevision) || expectedRevision !== artifact.revision) throw Error('Artifact changed; read its current revision before requesting saved bytes');
  if (!Number.isSafeInteger(offset) || offset < 0 || !Number.isSafeInteger(maxChars)
    || maxChars < 1 || maxChars > MAX_ARTIFACT_READ_CHARS) throw Error('Use a nonnegative character offset and a bounded maxChars');
  const text = typeof artifact.source?.content === 'string' ? artifact.source.content : artifact.text;
  if (offset > text.length) throw Error('Character offset exceeds the saved record');
  const end = Math.min(text.length, offset + maxChars);
  const content = text.slice(offset, end);
  return { artifactId, revision: artifact.revision, kind: artifact.kind, status: artifact.status,
    classification: 'saved record excerpt; no new observation or permission',
    sourceSha256: artifact.source?.sha256 ?? null, contentHash: artifact.contentHash,
    offsetUnit: 'UTF-16 code units', offset, end, totalChars: text.length, nextOffset: end < text.length ? end : null,
    content, sliceSha256: sha256Hex(content) };
}
