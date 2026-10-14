// Fawn workspace — owner session for workflow adapters
//
// Binds one owner's credential and generation to the service API. Holds no authority of its
// own: every mutation is still admitted and dispatched by the service.

/**
 * @param {import('../service.js').WorkspaceService} service
 * @param {{workspaceId: string, ownerId: string, ownerGeneration: number, ownerCredential: string}} owner
 */
function createOwnerSession(service, owner) {
  const identity = Object.freeze({
    workspaceId: owner.workspaceId,
    ownerId: owner.ownerId,
    ownerGeneration: owner.ownerGeneration,
    ownerCredential: owner.ownerCredential,
  });
  return Object.freeze({
    identity,
    observe: () => service.observe(identity),
    submit: (observation, spec) => service.submitAction({
      ...identity,
      observationVersion: observation.observationVersion,
      ...spec,
    }),
    async act(observation, spec) {
      const admitted = await this.submit(observation, spec);
      return admitted.result;
    },
    acknowledge: (actionId) => service.acknowledge({ ...identity, actionId }),
    reconcile: (spec) => service.reconcile({ ...identity, ...spec }),
    outstanding: () => service.outstanding(identity),
    action: (actionId) => service.action({ ...identity, actionId }),
  });
}

function findElement(observation, predicate, description) {
  const element = observation.elements.find(predicate);
  if (!element) throw new Error(`observation ${observation.observationVersion}: no element ${description}`);
  return { selector: element.selector, digest: element.digest };
}

export { createOwnerSession, findElement };
