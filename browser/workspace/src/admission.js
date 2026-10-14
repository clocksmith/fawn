// Fawn workspace — admission and dispatch-time checks
//
// The same checks run when intent is admitted and again immediately before dispatch, so every
// workflow adapter receives identical rejections for stale or unauthorized commands.

import { generationForCredential, credentialMatches } from './authority.js';
import { validateConditions } from './conditions.js';
import { REJECTION_CODES, workspaceError } from './errors.js';
import { ACTION_STATUS, OUTCOMES, latestReconciliation } from './state.js';

const URL_TARGET_KINDS = Object.freeze(['navigate']);
const VALUE_KINDS = Object.freeze(['fill', 'select']);
const COMPLETION_REQUIRED_KINDS = Object.freeze(['submit']);
const FORM_CONTROL_KINDS = Object.freeze(['submit']);

function reject(code, message, details) {
  throw workspaceError(code, message, details);
}

function requireString(value, path) {
  if (typeof value !== 'string' || value.length === 0) {
    reject(REJECTION_CODES.INVALID_COMMAND, `${path}: expected non-empty string`, {
      path,
      expected: 'non-empty string',
      received: value,
    });
  }
}

function originOf(url, path) {
  try {
    return new URL(url).origin;
  } catch {
    return reject(REJECTION_CODES.INVALID_COMMAND, `${path}: expected absolute URL`, { path, received: url });
  }
}

function authenticateAdmin(workspace, adminCredential) {
  if (!credentialMatches(adminCredential, workspace.adminCredentialHash)) {
    reject(REJECTION_CODES.UNAUTHENTICATED, 'admin credential rejected', { workspaceId: workspace.workspaceId });
  }
}

/**
 * Authenticate an owner and require the claimed generation to be the current one.
 */
function authenticateOwner(workspace, { ownerCredential, ownerId, ownerGeneration }) {
  if (workspace.ownerGeneration === 0) {
    reject(REJECTION_CODES.NO_OWNER, 'workspace has no owner', { workspaceId: workspace.workspaceId });
  }
  const issuedGeneration = generationForCredential(ownerCredential, workspace.ownerCredentialHashes);
  if (issuedGeneration === null || issuedGeneration !== ownerGeneration) {
    reject(REJECTION_CODES.UNAUTHENTICATED, 'owner credential rejected', {
      workspaceId: workspace.workspaceId,
      claimedGeneration: ownerGeneration,
    });
  }
  if (ownerGeneration !== workspace.ownerGeneration || ownerId !== workspace.ownerId) {
    reject(REJECTION_CODES.STALE_OWNER_GENERATION, 'owner generation is no longer current', {
      workspaceId: workspace.workspaceId,
      claimed: { ownerId, ownerGeneration },
      current: { ownerId: workspace.ownerId, ownerGeneration: workspace.ownerGeneration },
    });
  }
}

function validateActionShape(command, config) {
  requireString(command.actionId, 'actionId');
  requireString(command.authorizedIntent, 'authorizedIntent');
  if (!config.actionKinds.includes(command.kind)) {
    reject(REJECTION_CODES.UNSUPPORTED_ACTION_KIND, `kind ${command.kind}`, {
      path: 'kind',
      expected: config.actionKinds,
      received: command.kind,
    });
  }
  if (URL_TARGET_KINDS.includes(command.kind)) {
    requireString(command.target?.url, 'target.url');
  } else {
    requireString(command.target?.selector, 'target.selector');
    requireString(command.target?.digest, 'target.digest');
  }
  if (VALUE_KINDS.includes(command.kind)) requireString(command.params?.value, 'params.value');
  if (!Number.isInteger(command.observationVersion)) {
    reject(REJECTION_CODES.INVALID_COMMAND, 'observationVersion: expected integer', {
      path: 'observationVersion',
      received: command.observationVersion,
    });
  }
  validateConditions(command.preconditions ?? [], config.conditionKinds, 'preconditions');
  const effect = command.effect ?? { external: false };
  if (typeof effect.external !== 'boolean') {
    reject(REJECTION_CODES.INVALID_COMMAND, 'effect.external: expected boolean', { path: 'effect.external' });
  }
  const completedWhen = command.completion?.completedWhen ?? [];
  validateConditions(completedWhen, config.conditionKinds, 'completion.completedWhen');
  validateConditions(command.completion?.failedWhen ?? [], config.conditionKinds, 'completion.failedWhen');
  const needsCompletion = effect.external || COMPLETION_REQUIRED_KINDS.includes(command.kind);
  if (needsCompletion && completedWhen.length === 0) {
    reject(REJECTION_CODES.INVALID_COMMAND, 'completion.completedWhen: required for external effects', {
      path: 'completion.completedWhen',
    });
  }
  if (effect.external) requireString(effect.effectKey, 'effect.effectKey');
  if (effect.externalIdempotencyKey !== undefined) {
    requireString(effect.externalIdempotencyKey, 'effect.externalIdempotencyKey');
    if (effect.externalIdempotencyKey === command.actionId) {
      reject(REJECTION_CODES.INVALID_IDEMPOTENCY_KEY, 'a local action id is not an external idempotency key', {
        path: 'effect.externalIdempotencyKey',
      });
    }
  }
}

function targetOrigin(command, workspace) {
  if (URL_TARGET_KINDS.includes(command.kind)) return originOf(command.target.url, 'target.url');
  return workspace.observation ? originOf(workspace.observation.url, 'observation.url') : null;
}

/**
 * Require a current, unexpired grant covering intent, action kind and every target origin.
 */
function authorize(workspace, command, nowMs) {
  const live = [...workspace.grants.values()].filter(
    (grant) => grant.ownerId === command.ownerId
      && (grant.expiresAt === null || Date.parse(grant.expiresAt) > nowMs),
  );
  const forIntent = live.filter((grant) => grant.intents.includes(command.authorizedIntent));
  if (forIntent.length === 0) {
    reject(REJECTION_CODES.UNAUTHORIZED_INTENT, `intent ${command.authorizedIntent}`, {
      ownerId: command.ownerId,
      authorizedIntent: command.authorizedIntent,
    });
  }
  const origins = [targetOrigin(command, workspace)];
  const formAction = workspace.observation?.targetFormActions?.[command.target?.selector];
  if (command.kind === 'submit' && formAction) origins.push(originOf(formAction, 'target.formAction'));
  const grant = forIntent.find((candidate) => candidate.actionKinds.includes(command.kind)
    && origins.every((origin) => origin !== null && candidate.targetOrigins.includes(origin)));
  if (!grant) {
    reject(REJECTION_CODES.UNAUTHORIZED_TARGET, `${command.kind} on ${origins.join(', ')}`, {
      kind: command.kind,
      origins,
    });
  }
  return grant.grantId;
}

/**
 * Require the action's observation to be the workspace's current, unexpired observation under
 * the current owner generation, and its element target to appear in that observation.
 */
function checkObservation(workspace, command, nowMs) {
  const observation = workspace.observation;
  if (!observation || command.observationVersion > workspace.observationVersion || command.observationVersion < 1) {
    reject(REJECTION_CODES.UNKNOWN_OBSERVATION, `observation ${command.observationVersion}`, {
      observationVersion: command.observationVersion,
    });
  }
  if (command.observationVersion !== workspace.observationVersion
    || observation.ownerGeneration !== workspace.ownerGeneration) {
    reject(REJECTION_CODES.STALE_OBSERVATION, `observation ${command.observationVersion} is not current`, {
      observationVersion: command.observationVersion,
      currentVersion: workspace.observationVersion,
      observationOwnerGeneration: observation.ownerGeneration,
    });
  }
  if (Date.parse(observation.expiresAt) <= nowMs) {
    reject(REJECTION_CODES.EXPIRED_OBSERVATION, `observation ${observation.observationVersion} expired`, {
      expiresAt: observation.expiresAt,
    });
  }
  if (!URL_TARGET_KINDS.includes(command.kind)) {
    if (observation.targetFormActions?.[command.target.selector] && !FORM_CONTROL_KINDS.includes(command.kind)) {
      reject(REJECTION_CODES.INVALID_COMMAND, `${command.target.selector} submits a form; use kind submit`, {
        selector: command.target.selector,
        expected: FORM_CONTROL_KINDS,
        received: command.kind,
      });
    }
    const digest = observation.targetDigests?.[command.target.selector];
    if (!digest) {
      reject(REJECTION_CODES.UNKNOWN_TARGET, `target ${command.target.selector} not in observation`, {
        selector: command.target.selector,
      });
    }
    if (digest !== command.target.digest) {
      reject(REJECTION_CODES.TARGET_CHANGED, `target ${command.target.selector} digest differs`, {
        selector: command.target.selector,
      });
    }
  }
}

function priorsForEffect(workspace, effectKey, excludeActionId) {
  return [...workspace.actions.values()].filter((action) => action.request.effect?.effectKey === effectKey
    && action.request.actionId !== excludeActionId);
}

function effectDisposition(action) {
  const reconciliation = latestReconciliation(action);
  if (action.status === ACTION_STATUS.QUEUED || (action.status === ACTION_STATUS.DISPATCHED && !action.outcome)) {
    return { blocking: REJECTION_CODES.EFFECT_UNRESOLVED };
  }
  if (action.status === ACTION_STATUS.NEVER_DISPATCHED) return { resubmittable: true };
  if (action.outcome.outcome === OUTCOMES.COMPLETED) return { blocking: REJECTION_CODES.EFFECT_ALREADY_COMPLETED };
  if (action.outcome.outcome === OUTCOMES.FAILED) return { resubmittable: true };
  if (reconciliation?.result === 'effect-present') return { blocking: REJECTION_CODES.EFFECT_ALREADY_COMPLETED };
  if (reconciliation?.permitsResubmission) {
    return { resubmittable: true, requiredIdempotencyKey: reconciliation.requiredIdempotencyKey ?? null };
  }
  return { blocking: REJECTION_CODES.EFFECT_UNRESOLVED };
}

/**
 * Missing completion never authorizes another submission: a command for an external effect
 * that already has an uncertain, in-flight or completed attempt is refused, and any permitted
 * resubmission must name the prior attempt explicitly.
 */
function checkEffect(workspace, command) {
  const effectKey = command.effect?.effectKey;
  if (!effectKey) {
    if (command.resubmissionOf) {
      reject(REJECTION_CODES.RESUBMISSION_NOT_PERMITTED, 'resubmission requires an effect key', {
        resubmissionOf: command.resubmissionOf,
      });
    }
    return;
  }
  const priors = priorsForEffect(workspace, effectKey, command.actionId);
  for (const prior of priors) {
    const disposition = effectDisposition(prior);
    if (disposition.blocking) {
      reject(disposition.blocking, `effect ${effectKey} has attempt ${prior.request.actionId}`, {
        effectKey,
        priorActionId: prior.request.actionId,
      });
    }
  }
  if (priors.length === 0) {
    if (command.resubmissionOf) {
      reject(REJECTION_CODES.RESUBMISSION_NOT_PERMITTED, 'no prior attempt for this effect', {
        resubmissionOf: command.resubmissionOf,
      });
    }
    return;
  }
  const latest = priors.reduce((a, b) => (a.admittedSeq > b.admittedSeq ? a : b));
  if (command.resubmissionOf !== latest.request.actionId) {
    reject(REJECTION_CODES.RESUBMISSION_NOT_PERMITTED, `name prior attempt ${latest.request.actionId}`, {
      effectKey,
      priorActionId: latest.request.actionId,
      received: command.resubmissionOf ?? null,
    });
  }
  const required = effectDisposition(latest).requiredIdempotencyKey;
  if (required && command.effect.externalIdempotencyKey !== required) {
    reject(REJECTION_CODES.RESUBMISSION_NOT_PERMITTED, 'resubmission must reuse the external idempotency key', {
      effectKey,
    });
  }
}

/**
 * Admission: shape, owner authority, authorization, observation binding and effect safety.
 */
function checkAdmission(workspace, command, config, nowMs) {
  validateActionShape(command, config);
  authenticateOwner(workspace, command);
  if (workspace.actions.has(command.actionId)) {
    reject(REJECTION_CODES.DUPLICATE_ACTION_ID, command.actionId, { actionId: command.actionId });
  }
  checkObservation(workspace, command, nowMs);
  const grantId = authorize(workspace, command, nowMs);
  checkEffect(workspace, command);
  return { grantId };
}

/**
 * Dispatch-time authority: generation, owner and grant must still hold for the admitted request.
 */
function checkDispatchAuthority(workspace, request, nowMs) {
  if (request.ownerGeneration !== workspace.ownerGeneration || request.ownerId !== workspace.ownerId) {
    reject(REJECTION_CODES.STALE_OWNER_GENERATION, 'owner generation advanced before dispatch', {
      admittedGeneration: request.ownerGeneration,
      currentGeneration: workspace.ownerGeneration,
    });
  }
  checkObservation(workspace, request, nowMs);
  authorize(workspace, request, nowMs);
}

export { authenticateAdmin, authenticateOwner, checkAdmission, checkDispatchAuthority };
