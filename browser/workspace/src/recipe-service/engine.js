// Managed recipe graph engine: PII-free sub-recipe authoring, cross-template composition,
// quorum-gated trace ingestion, whole-graph detour compression and empirical Bellman DP serving.
import { createRecipeGraphStore } from './graph-store.js';
import { createHash, createHmac } from 'node:crypto';
import {
  analyzeTraceGraph,
  canonicalJson,
  compressTraceRecords,
  reduceTraceGraph,
  solveRecipeDp,
} from '../client/trace-graph-dp.js';

const VERSION = 3;
const HASH_RE = /^[a-f0-9]{64}$/;
const UUID_RE = /^[a-zA-Z0-9._:-]{6,128}$/;
const IDENT_RE = /^[a-zA-Z0-9._:/-]{1,128}$/;
const KINDS = new Set(['fill', 'select', 'click']);
const FORBIDDEN_INPUT_TYPES = new Set(['submit', 'image', 'file', 'password', 'hidden']);
const ALLOWED_STEP_KEYS = new Set([
  'beforeOutline',
  'afterOutline',
  'kind',
  'locator',
  'slot',
  'beforeSlots',
  'beforeClicks',
  'elapsedMs',
  'outcome',
]);
const DEFAULT_LIMITS = Object.freeze({ maxNodes: 256, maxEdges: 512, maxSteps: 32 });
const DEFAULT_REPLAN_PENALTY = 1000;

function serviceError(status, code, message) {
  return Object.assign(Error(message), { status, code });
}

function sha256Hex(text) {
  return createHash('sha256').update(text).digest('hex');
}

function hmacHex(key, text) {
  return createHmac('sha256', key).update(text).digest('hex');
}

function dagHash(dagKey, value) {
  return hmacHex(dagKey, canonicalJson(value));
}

function validateOrigin(origin) {
  if (typeof origin !== 'string' || !origin.trim() || origin.length > 256) {
    throw serviceError(400, 'invalid-origin', 'Explicit http(s) origin is required');
  }
  let parsed;
  try { parsed = new URL(origin); }
  catch { throw serviceError(400, 'invalid-origin', 'Malformed origin URL'); }
  if (!['http:', 'https:'].includes(parsed.protocol) || parsed.origin !== origin) {
    throw serviceError(400, 'non-origin-url-rejected', 'Origin must exclude paths, queries, fragments and credentials');
  }
  return parsed.origin;
}

function validateIdentifier(value, field) {
  if (typeof value !== 'string' || !IDENT_RE.test(value)) {
    throw serviceError(400, `invalid-${field}`, `Invalid ${field} identifier`);
  }
  return value;
}

function validateScope(scope) {
  if (scope !== 'workspace' && scope !== 'catalog') {
    throw serviceError(400, 'invalid-scope', 'Scope must be workspace or catalog');
  }
  return scope;
}

function validateLimits(limits = DEFAULT_LIMITS) {
  if (!limits || typeof limits !== 'object' || Array.isArray(limits)
    || Object.keys(limits).sort().join() !== 'maxEdges,maxNodes,maxSteps'
    || Object.values(limits).some(v => !Number.isSafeInteger(v) || v < 1 || v > 8192)) {
    throw serviceError(400, 'invalid-limits', 'Limits require bounded positive maxNodes, maxEdges and maxSteps');
  }
  return { maxNodes: limits.maxNodes, maxEdges: limits.maxEdges, maxSteps: limits.maxSteps };
}

function validateLocator(locator) {
  if (!Array.isArray(locator) || locator.length !== 4
    || locator.some(part => typeof part !== 'string' || part.length > 256)) {
    throw serviceError(400, 'invalid-locator', 'Locator must be a 4-string structural tuple [role, label, tag, inputType]');
  }
  const [, label, , inputType] = locator;
  if (!label.trim()) {
    throw serviceError(400, 'unnamed-target-rejected', 'Structural locator requires a non-empty label');
  }
  if (FORBIDDEN_INPUT_TYPES.has(inputType.toLowerCase())) {
    throw serviceError(400, 'forbidden-target-rejected', `Input type ${inputType} cannot be stored in recipes`);
  }
  return locator;
}

function validateRawStep(step, index) {
  if (!step || typeof step !== 'object' || Array.isArray(step)) {
    throw serviceError(400, 'invalid-step', `Step ${index} must be an object`);
  }
  for (const key of Object.keys(step)) {
    if (!ALLOWED_STEP_KEYS.has(key)) {
      throw serviceError(400, 'pii-or-unknown-field-rejected',
        `Step ${index} contains disallowed field "${key}"; raw values, selectors and URLs are forbidden`);
    }
  }
  if (!HASH_RE.test(step.beforeOutline) || !HASH_RE.test(step.afterOutline)) {
    throw serviceError(400, 'invalid-outline-hash', `Step ${index} requires 64-hex beforeOutline and afterOutline`);
  }
  if (!KINDS.has(step.kind)) {
    throw serviceError(400, 'invalid-step-kind', `Step ${index} kind must be fill, select or click`);
  }
  const locator = validateLocator(step.locator);
  let slot = null;
  if (step.kind === 'click') {
    if (step.slot !== undefined && step.slot !== null) {
      throw serviceError(400, 'invalid-click-slot', `Click step ${index} must not declare a slot`);
    }
  } else {
    slot = validateIdentifier(step.slot, 'slot');
  }
  const beforeSlots = step.beforeSlots === undefined ? null : step.beforeSlots;
  if (beforeSlots !== null && (!Array.isArray(beforeSlots) || beforeSlots.some(s => typeof s !== 'string' || !IDENT_RE.test(s)))) {
    throw serviceError(400, 'invalid-before-slots', `Step ${index} beforeSlots must be slot identifiers`);
  }
  const beforeClicks = step.beforeClicks ?? null;
  if (beforeClicks !== null && (!Number.isSafeInteger(beforeClicks) || beforeClicks < 0 || beforeClicks > 64)) {
    throw serviceError(400, 'invalid-before-clicks', `Step ${index} beforeClicks must be a non-negative integer`);
  }
  const elapsedMs = step.elapsedMs ?? 0;
  if (typeof elapsedMs !== 'number' || !Number.isFinite(elapsedMs) || elapsedMs < 0 || elapsedMs > 300000) {
    throw serviceError(400, 'invalid-elapsed-ms', `Step ${index} elapsedMs out of bounds`);
  }
  const outcome = step.outcome ?? 'completed';
  if (outcome !== 'completed' && outcome !== 'miss') {
    throw serviceError(400, 'invalid-step-outcome', `Step ${index} outcome must be completed or miss`);
  }
  return {
    beforeOutline: step.beforeOutline,
    afterOutline: step.afterOutline,
    kind: step.kind,
    locator,
    slot,
    beforeSlots: beforeSlots ? [...new Set(beforeSlots)].sort() : null,
    beforeClicks,
    elapsedMs,
    outcome,
  };
}

function computeStateNodeId(dagKey, { template, origin, readScope = null, outlineHash, slotNames, sameOutlineClicks }) {
  const sortedSlotHashes = [...new Set(slotNames.map(slot => dagHash(dagKey, ['slot', slot])))].sort();
  const baseScope = [template, origin, readScope];
  const stateKey = ['state-v3', sortedSlotHashes, sameOutlineClicks];
  return {
    nodeId: dagHash(dagKey, [baseScope, stateKey, outlineHash]),
    sortedSlots: [...new Set(slotNames)].sort(),
  };
}

/** Materialize structural transitions into TraceDag v3 records and contract any intra-trace loops. */
function materializeSteps(dagKey, { template, origin, readScope = null }, validatedSteps) {
  let currentSlots = validatedSteps[0]?.beforeSlots ?? [];
  let currentClicks = validatedSteps[0]?.beforeClicks ?? 0;
  const rawRecords = [];

  for (let i = 0; i < validatedSteps.length; i++) {
    const s = validatedSteps[i];
    if (i > 0 && validatedSteps[i - 1].afterOutline !== s.beforeOutline) {
      throw serviceError(400, 'disconnected-trace-steps', `Step ${i} beforeOutline does not match prior afterOutline`);
    }
    const beforeSlots = s.beforeSlots ?? currentSlots;
    const beforeClicks = s.beforeClicks ?? currentClicks;
    const beforeNode = computeStateNodeId(dagKey, {
      template, origin, readScope, outlineHash: s.beforeOutline,
      slotNames: beforeSlots, sameOutlineClicks: beforeClicks,
    });

    let afterSlots;
    let afterClicks;
    if (s.afterOutline !== s.beforeOutline) {
      afterSlots = [];
      afterClicks = 0;
    } else if (s.kind === 'click') {
      afterSlots = beforeNode.sortedSlots;
      afterClicks = beforeClicks + 1;
    } else {
      afterSlots = [...new Set([...beforeNode.sortedSlots, s.slot])].sort();
      afterClicks = beforeClicks;
    }
    const afterNode = computeStateNodeId(dagKey, {
      template, origin, readScope, outlineHash: s.afterOutline,
      slotNames: afterSlots, sameOutlineClicks: afterClicks,
    });

    const stepObj = {
      kind: s.kind,
      locator: dagHash(dagKey, ['locator', s.locator]),
      slot: s.kind === 'click' ? null : dagHash(dagKey, ['slot', s.slot]),
    };

    rawRecords.push({
      id: beforeNode.nodeId,
      depth: i,
      step: stepObj,
      next: afterNode.nodeId,
      edgeKey: sha256Hex(canonicalJson([beforeNode.nodeId, afterNode.nodeId, s.kind, s.locator, s.slot])),
      meta: {
        beforeOutline: s.beforeOutline,
        beforeSlots: beforeNode.sortedSlots,
        beforeClicks,
        afterOutline: s.afterOutline,
        afterSlots: afterNode.sortedSlots,
        afterClicks,
        kind: s.kind,
        locator: s.locator,
        slot: s.slot,
        elapsedMs: s.elapsedMs,
        outcome: s.outcome,
      },
    });
    currentSlots = afterNode.sortedSlots;
    currentClicks = afterClicks;
  }

  const last = rawRecords.at(-1);
  const terminal = last.meta.outcome === 'completed'
    ? { id: last.next, depth: rawRecords.length }
    : null;
  const compressed = compressTraceRecords(rawRecords, terminal);
  return { records: compressed.records, terminal: compressed.terminal, contractedSteps: compressed.contractedSteps };
}

function empiricalEdgeWeight(edgeRow, replanPenalty = DEFAULT_REPLAN_PENALTY) {
  const attempts = Math.max(1, Number(edgeRow.attempts ?? 1));
  const completions = Math.min(attempts, Math.max(0, Number(edgeRow.completions ?? 1)));
  const avgMs = Math.max(0, Number(edgeRow.total_cost_ms ?? 0)) / attempts;
  const latencyTieBreaker = 1e-3 * Math.min(1, avgMs / 10000);
  const missRate = (attempts - completions) / attempts;
  return Number((1 + latencyTieBreaker + missRate * replanPenalty).toFixed(6));
}

export function createManagedRecipeEngine({
  store,
  secret,
  catalogQuorum = 2,
  replanPenalty = DEFAULT_REPLAN_PENALTY,
}) {
  if (!store) throw Error('Recipe store adapter is required');
  if (typeof secret !== 'string' || secret.length < 32) {
    throw Error('Managed recipe engine requires a >=32-character secret');
  }
  if (!Number.isSafeInteger(catalogQuorum) || catalogQuorum < 1) {
    throw Error('catalogQuorum must be a positive integer');
  }

  function resolveNamespaceMeta(principal, { scope, origin, template, readScope = null }) {
    const validScope = validateScope(scope);
    const validOrigin = validateOrigin(origin);
    const validTemplate = validateIdentifier(template, 'template');
    const normalizedReadScope = readScope === null || readScope === undefined
      ? null
      : validateIdentifier(readScope, 'readScope');
    const workspaceOwner = validScope === 'catalog' ? 'shared-catalog' : principal.workspaceId;
    const namespaceId = sha256Hex(canonicalJson([
      'fawn-recipe-ns-v1',
      validScope,
      workspaceOwner,
      validOrigin,
      validTemplate,
      normalizedReadScope,
    ]));
    const dagKey = hmacHex(secret, canonicalJson(['fawn-recipe-key-v1', namespaceId]));
    return {
      namespaceId,
      scope: validScope,
      workspaceOwner,
      origin: validOrigin,
      template: validTemplate,
      readScope: normalizedReadScope,
      dagKey,
    };
  }

  function signEnvelope(payload) {
    return hmacHex(secret, canonicalJson(['fawn-recipe-envelope-v1', payload]));
  }

  function reduceRows(meta, limits, nodeRows, edgeRows) {
    const candidateNodes = new Map();
    for (const row of nodeRows) {
      candidateNodes.set(row.node_id, {
        depth: Number(row.depth ?? 0),
        edges: [],
        repairs: [],
        terminal: Boolean(row.is_terminal),
      });
    }
    const edgeByKey = new Map();
    for (const row of edgeRows) {
      if (!candidateNodes.has(row.from_node) || !candidateNodes.has(row.to_node)) continue;
      const step = {
        kind: row.step_kind,
        locator: dagHash(meta.dagKey, ['locator', JSON.parse(row.locator_json)]),
        slot: row.step_kind === 'click' ? null : dagHash(meta.dagKey, ['slot', row.slot_name]),
      };
      const weight = empiricalEdgeWeight(row, replanPenalty);
      candidateNodes.get(row.from_node).edges.push({
        next: row.to_node,
        step,
        weight,
      });
      edgeByKey.set(`${row.from_node}->${row.to_node}:${canonicalJson(step)}`, row.edge_key);
    }

    const reduced = reduceTraceGraph(candidateNodes, limits, {
      pruneDeadEnds: true,
      pruneDetours: true,
    });
    const inDeg = new Map([...reduced.nodes.keys()].map(id => [id, 0]));
    for (const node of reduced.nodes.values()) {
      for (const edge of node.edges) inDeg.set(edge.next, (inDeg.get(edge.next) ?? 0) + 1);
      for (const next of node.repairs ?? []) inDeg.set(next, (inDeg.get(next) ?? 0) + 1);
    }
    for (const [id, node] of [...reduced.nodes.entries()]) {
      if ((inDeg.get(id) ?? 0) === 0 && node.edges.length === 0 && (node.repairs?.length ?? 0) === 0) {
        reduced.nodes.delete(id);
      }
    }

    const survivingEdges = new Set();
    for (const [id, node] of reduced.nodes) {
      for (const edge of node.edges) {
        survivingEdges.add(edgeByKey.get(`${id}->${edge.next}:${canonicalJson(edge.step)}`));
      }
    }
    return { ...reduced, survivingEdges };
  }

  const graphs = createRecipeGraphStore(store, reduceRows, catalogQuorum);
  return {
    signEnvelope,
    async registerSubrecipe(principal, body) {
      if (!body || typeof body !== 'object' || Array.isArray(body)) {
        throw serviceError(400, 'invalid-body', 'JSON object required');
      }
      const scope = validateScope(body.scope ?? 'workspace');
      if (scope === 'catalog' && principal.role !== 'publisher') {
        throw serviceError(403, 'publisher-role-required', 'Catalog sub-recipe authoring requires publisher role');
      }
      const origin = validateOrigin(body.origin);
      const name = validateIdentifier(body.name, 'name');
      if (!Array.isArray(body.steps) || body.steps.length < 1 || body.steps.length > 64) {
        throw serviceError(400, 'invalid-steps', 'Sub-recipe requires 1..64 structural steps');
      }
      const steps = body.steps.map((step, index) => validateRawStep(step, index));
      if (steps.some(step => step.outcome !== 'completed')) {
        throw serviceError(400, 'invalid-subrecipe-outcome', 'Authored sub-recipe steps must have completed outcomes');
      }
      // Dry-run materialization + acyclicity check before saving.
      const tempKey = hmacHex(secret, canonicalJson(['subrecipe-validate', origin, name]));
      const materialized = materializeSteps(tempKey, { template: name, origin, readScope: null }, steps);
      const workspaceOwner = scope === 'catalog' ? 'shared-catalog' : principal.workspaceId;
      const subrecipeId = sha256Hex(canonicalJson(['fawn-subrecipe-v1', scope, workspaceOwner, origin, name]));
      const now = new Date().toISOString();
      const saved = await store.get(
        `INSERT INTO recipe_subrecipes
          (subrecipe_id, scope_kind, workspace_id, origin, name, entry_outline, exit_outline, steps_json, revision, updated_at)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?, 1, ?)
         ON CONFLICT(subrecipe_id) DO UPDATE SET
           entry_outline = excluded.entry_outline,
           exit_outline = excluded.exit_outline,
           steps_json = excluded.steps_json,
           revision = recipe_subrecipes.revision + 1,
           updated_at = excluded.updated_at RETURNING revision`,
        [
          subrecipeId, scope, workspaceOwner, origin, name,
          steps[0].beforeOutline, steps.at(-1).afterOutline,
          canonicalJson(steps), now,
        ],
      );
      return {
        subrecipeId,
        scope,
        origin,
        name,
        revision: saved.revision,
        entryOutline: steps[0].beforeOutline,
        exitOutline: steps.at(-1).afterOutline,
        stepCount: materialized.records.length,
        contractedSteps: materialized.contractedSteps,
      };
    },

    async listSubrecipes(principal, { scope = 'workspace', origin }) {
      const validScope = validateScope(scope);
      const validOrigin = validateOrigin(origin);
      const workspaceOwner = validScope === 'catalog' ? 'shared-catalog' : principal.workspaceId;
      const rows = await store.all(
        `SELECT subrecipe_id, scope_kind, origin, name, entry_outline, exit_outline, revision, updated_at
         FROM recipe_subrecipes
         WHERE scope_kind = ? AND workspace_id = ? AND origin = ?
         ORDER BY name ASC`,
        [validScope, workspaceOwner, validOrigin],
      );
      return {
        scope: validScope,
        origin: validOrigin,
        subrecipes: rows.map(row => ({
          subrecipeId: row.subrecipe_id,
          name: row.name,
          entryOutline: row.entry_outline,
          exitOutline: row.exit_outline,
          revision: Number(row.revision),
          updatedAt: row.updated_at,
        })),
      };
    },

    async ingestTrace(principal, body) {
      if (!body || typeof body !== 'object' || Array.isArray(body)) {
        throw serviceError(400, 'invalid-body', 'JSON object required');
      }
      const submissionId = typeof body.submissionId === 'string' && UUID_RE.test(body.submissionId)
        ? body.submissionId
        : null;
      if (!submissionId) {
        throw serviceError(400, 'invalid-submission-id', 'Bounded submissionId is required for idempotent ingestion');
      }
      const verificationId = validateIdentifier(body.verificationId, 'verificationId');
      if (body.reviewVerified !== true) {
        throw serviceError(400, 'unverified-trace-rejected', 'Only independently reviewVerified traces may be ingested');
      }
      const limits = validateLimits(body.limits ?? DEFAULT_LIMITS);
      const meta = resolveNamespaceMeta(principal, {
        scope: body.scope ?? 'workspace',
        origin: body.origin,
        template: body.template,
        readScope: body.readScope ?? null,
      });
      const authorHash = sha256Hex(canonicalJson(['author-v1', principal.workspaceId, principal.clientId]));
      if (!Array.isArray(body.steps) || body.steps.length > limits.maxSteps) {
        throw serviceError(400, 'invalid-steps', `Trace steps must contain 1..${limits.maxSteps} transitions`);
      }
      const steps = body.steps.map(validateRawStep);
      const requestHash = sha256Hex(canonicalJson([verificationId, steps, limits]));
      const receiptKey = [meta.namespaceId, authorHash, submissionId];
      async function priorResponse() {
        const row = await store.get(
          'SELECT * FROM recipe_submission_receipts WHERE namespace_id = ? AND author_hash = ? AND submission_id = ?',
          receiptKey,
        );
        if (row) {
          if (row.verification_id !== verificationId || (steps.length && row.request_hash !== requestHash)) {
            throw serviceError(409, 'submission-conflict', 'Submission ID already used for a different trace');
          }
          return JSON.parse(row.response_json);
        }
        // Older receipts lack a request digest and a catalog contributor's workspace.
        const legacy = await store.get(
          'SELECT * FROM recipe_submissions WHERE submission_id = ? AND namespace_id = ? AND client_id = ?',
          [submissionId, meta.namespaceId, principal.clientId],
        );
        if (!legacy) return null;
        if (meta.scope === 'workspace' && legacy.verification_id === verificationId && !steps.length) {
          return JSON.parse(legacy.response_json);
        }
        throw serviceError(409, 'legacy-submission', 'Existing receipt cannot verify this payload');
      }
      if (!steps.length) {
        const prior = await priorResponse();
        if (prior) return prior;
        throw serviceError(400, 'invalid-steps', 'A new trace requires at least one step');
      }
      const segment = materializeSteps(meta.dagKey, meta, steps);
      return graphs.commit(meta, limits, [segment], {
        authorHash, forcePromote: principal.role === 'publisher', priorResponse,
        finish(revision, reduced, statements) {
          const response = {
            submissionId, namespaceId: meta.namespaceId, scope: meta.scope, origin: meta.origin,
            template: meta.template, revision, contractedSteps: segment.contractedSteps,
            reduction: reduced.stats, compressionStats: analyzeTraceGraph(reduced.nodes, limits),
          };
          statements.push({
            sql: `INSERT INTO recipe_submission_receipts
                  (namespace_id, author_hash, submission_id, verification_id, request_hash, response_json)
                  VALUES (?, ?, ?, ?, ?, ?)`,
            params: [...receiptKey, verificationId, requestHash, JSON.stringify(response)],
          });
          return response;
        },
      });
    },

    async composeRecipe(principal, body) {
      if (!body || typeof body !== 'object' || Array.isArray(body)) {
        throw serviceError(400, 'invalid-body', 'JSON object required');
      }
      const scope = validateScope(body.scope ?? 'workspace');
      if (scope === 'catalog' && principal.role !== 'publisher') {
        throw serviceError(403, 'publisher-role-required', 'Composing catalog recipes requires publisher role');
      }
      const limits = validateLimits(body.limits ?? DEFAULT_LIMITS);
      const meta = resolveNamespaceMeta(principal, {
        scope,
        origin: body.origin,
        template: body.targetTemplate,
        readScope: body.readScope ?? null,
      });
      if (!Array.isArray(body.subrecipes) || body.subrecipes.length < 1 || body.subrecipes.length > 32) {
        throw serviceError(400, 'invalid-subrecipe-list', 'subrecipes must name 1..32 registered sub-recipes');
      }
      const subrecipeNames = body.subrecipes.map(name => validateIdentifier(name, 'subrecipe'));
      const workspaceOwner = scope === 'catalog' ? 'shared-catalog' : principal.workspaceId;

      const segments = [];
      for (const name of subrecipeNames) {
        let row = await store.get(
          `SELECT steps_json FROM recipe_subrecipes
           WHERE scope_kind = ? AND workspace_id = ? AND origin = ? AND name = ?`,
          [scope, workspaceOwner, meta.origin, name],
        );
        if (!row && scope === 'workspace') {
          // Allow workspace compositions to pull promoted shared-catalog building blocks too.
          row = await store.get(
            `SELECT steps_json FROM recipe_subrecipes
             WHERE scope_kind = 'catalog' AND workspace_id = 'shared-catalog' AND origin = ? AND name = ?`,
            [meta.origin, name],
          );
        }
        if (!row) {
          throw serviceError(404, 'subrecipe-not-found', `Sub-recipe "${name}" not found for ${meta.origin}`);
        }
        segments.push(JSON.parse(row.steps_json));
      }

      // Materialize each sub-recipe under targetTemplate's dagKey and clear intermediate handoff terminals.
      const materializedSegments = segments.map(steps => materializeSteps(meta.dagKey, meta, steps));
      const nonTerminalSources = new Set();
      for (let i = 1; i < materializedSegments.length; i++) {
        for (const record of materializedSegments[i].records) {
          nonTerminalSources.add(record.id);
        }
      }
      const authorHash = sha256Hex(canonicalJson(['composer-v1', principal.workspaceId, principal.clientId]));
      for (const seg of materializedSegments) {
        if (seg.terminal && nonTerminalSources.has(seg.terminal.id)) {
          seg.clearTerminal = seg.terminal.id;
          seg.terminal = null;
        }
      }
      return graphs.commit(meta, limits, materializedSegments, {
        authorHash, forcePromote: true, promotedSummary: scope === 'catalog',
        finish(revision, reduced) {
          return {
            namespaceId: meta.namespaceId, scope: meta.scope, origin: meta.origin,
            template: meta.template, revision, composedSubrecipes: subrecipeNames,
            reduction: reduced.stats, compressionStats: analyzeTraceGraph(reduced.nodes, limits),
          };
        },
      });
    },

    async resolveRecipe(principal, body) {
      if (!body || typeof body !== 'object' || Array.isArray(body)) {
        throw serviceError(400, 'invalid-body', 'JSON object required');
      }
      const limits = validateLimits(body.limits ?? DEFAULT_LIMITS);
      const meta = resolveNamespaceMeta(principal, {
        scope: body.scope ?? 'workspace',
        origin: body.origin,
        template: body.template,
        readScope: body.readScope ?? null,
      });
      const availableSlotNames = body.availableSlots === undefined
        ? null
        : body.availableSlots;
      if (availableSlotNames !== null && (!Array.isArray(availableSlotNames)
        || availableSlotNames.some(s => typeof s !== 'string' || !IDENT_RE.test(s)))) {
        throw serviceError(400, 'invalid-available-slots', 'availableSlots must be an array of slot identifiers');
      }
      const currentOutline = body.currentOutline ?? null;
      if (currentOutline !== null && !HASH_RE.test(currentOutline)) {
        throw serviceError(400, 'invalid-current-outline', 'currentOutline must be a 64-hex structural hash');
      }

      const { revision, nodes: reducedNodes, stats: reduction } = await graphs.read(meta, limits);

      const availableSlotHashes = availableSlotNames === null
        ? null
        : new Set(availableSlotNames.map(slot => dagHash(meta.dagKey, ['slot', slot])));

      const dp = reducedNodes.size > 0
        ? solveRecipeDp(reducedNodes, limits, { availableSlots: availableSlotHashes, replanPenalty })
        : { values: new Map(), policy: new Map() };
      const compressionStats = reducedNodes.size > 0
        ? analyzeTraceGraph(reducedNodes, limits)
        : {
          nodeCount: 0, edgeCount: 0, repairBridgeCount: 0, terminalCount: 0,
          sharedMergeNodes: 0, confluentDiamonds: 0, maxDepth: 0, optimalRootCosts: {},
        };

      let entryNodeId = null;
      let entryOptimalCost = null;
      if (currentOutline) {
        const entry = computeStateNodeId(meta.dagKey, {
          template: meta.template,
          origin: meta.origin,
          readScope: meta.readScope,
          outlineHash: currentOutline,
          slotNames: body.currentFilledSlots ?? [],
          sameOutlineClicks: 0,
        });
        if (reducedNodes.has(entry.nodeId)) {
          entryNodeId = entry.nodeId;
          const val = dp.values.get(entry.nodeId);
          entryOptimalCost = Number.isFinite(val) ? val : null;
        }
      }

      const dag = {
        version: VERSION,
        key: meta.dagKey,
        limits,
        nodes: [...reducedNodes.entries()],
      };
      const dagDigest = sha256Hex(canonicalJson(dag));
      const signature = signEnvelope({
        namespaceId: meta.namespaceId,
        scope: meta.scope,
        origin: meta.origin,
        template: meta.template,
        revision,
        dagDigest,
      });

      return {
        namespaceId: meta.namespaceId,
        scope: meta.scope,
        origin: meta.origin,
        template: meta.template,
        revision,
        dag,
        dagDigest,
        signature,
        reduction,
        compressionStats,
        entryLookup: currentOutline ? { currentOutline, entryNodeId, entryOptimalCost } : null,
      };
    },
  };
}
