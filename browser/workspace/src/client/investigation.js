// Client-owned reasoning records; these never grant browser execution authority.
import { Journal, sha256Hex, canonicalJson } from '../journal.js';
import { randomUUID } from 'node:crypto';

const KINDS = ['evidence', 'assumption', 'finding', 'decision', 'question'];
export class InvestigationStore {
  constructor(directory) {
    this.journal = Journal.open({ directory, fileName: 'investigation.jsonl', lockFileName: 'investigation.lock' });
    this.projects = new Map();
    for (const record of this.journal.records) this.apply(record);
  }
  apply(record) {
    const { body, type } = record;
    if (type === 'project-created') this.projects.set(body.id, { ...body, artifacts: {}, activity: [], run: null });
    else {
      const project = this.projects.get(body.projectId);
      if (!project) throw Error(`Unknown investigation ${body.projectId}`);
      if (type === 'artifact-revised') {
        project.artifacts[body.artifact.id] = structuredClone(body.artifact);
        // Every dependent must be reviewed against the new revision, including transitive conclusions.
        const changed = new Set([body.artifact.id]);
        let more = true;
        while (more) {
          more = false;
          for (const artifact of Object.values(project.artifacts)) {
            if (!changed.has(artifact.id) && artifact.dependsOn.some(id => changed.has(id))) {
              changed.add(artifact.id); artifact.status = 'needs-review'; more = true;
            }
          }
        }
      } else if (type === 'run-updated') project.run = body.run;
      else if (type !== 'activity-recorded') throw Error(`Unknown investigation record ${type}`);
      project.activity.push({ seq: record.seq, at: record.at, type, ...body });
    }
  }
  append(type, body) { const record = this.journal.append(type, body); this.apply(record); return record; }
  create({ id, goal, repositoryRoot = null }) {
    if (this.projects.has(id) || typeof goal !== 'string' || !goal.trim()) throw Error('A unique investigation and goal are required');
    this.append('project-created', { id, goal: goal.trim(), repositoryRoot, createdAt: new Date().toISOString() });
    return this.get(id);
  }
  get(id) { const project = this.projects.get(id); if (!project) throw Error('Unknown investigation'); return structuredClone(project); }
  revise(projectId, { id = randomUUID(), kind, text, dependsOn = [], source = null, expectedRevision = 0 }) {
    if (typeof id !== 'string' || !/^[a-z0-9][a-z0-9:._-]{0,127}$/i.test(id) || ['constructor', 'prototype'].includes(id)) throw Error('Invalid artifact identity');
    const project = this.get(projectId), previous = Object.hasOwn(project.artifacts, id) ? project.artifacts[id] : null;
    if (!KINDS.includes(kind) || typeof text !== 'string' || !text.trim()) throw Error('Artifact kind and text are required');
    if ((previous?.revision ?? 0) !== expectedRevision) throw Error('Artifact changed; reload before editing');
    if (!Array.isArray(dependsOn) || new Set(dependsOn).size !== dependsOn.length
      || dependsOn.some(parent => parent === id || !Object.hasOwn(project.artifacts, parent))) throw Error('Dependencies must name distinct existing artifacts');
    const reaches = (start, seen = new Set()) => {
      if (start === id) return true;
      if (seen.has(start)) return false;
      seen.add(start); return project.artifacts[start].dependsOn.some(parent => reaches(parent, seen));
    };
    if (dependsOn.some(parent => reaches(parent))) throw Error('Artifact dependencies cannot form a cycle');
    if (kind === 'evidence' && (!source || source.sha256 !== sha256Hex(source.content ?? ''))) throw Error('Evidence must bind the saved source bytes');
    const dependencyRevisions = Object.fromEntries(dependsOn.map(parent => [parent, project.artifacts[parent].revision]));
    const artifact = { id, kind, text: text.trim(), dependsOn, dependencyRevisions, source,
      revision: expectedRevision + 1, status: 'current' };
    const { status, ...revision } = artifact;
    artifact.contentHash = sha256Hex(canonicalJson(revision));
    this.append('artifact-revised', { projectId, artifact }); return artifact;
  }
  activity(projectId, event) { return this.append('activity-recorded', { projectId, ...event }); }
  run(projectId, run) { this.append('run-updated', { projectId, run }); }
  close() { this.journal.close(); }
}
