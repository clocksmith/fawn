// Trusted semantic groups narrow cache identity; they never replace service admission.
import { canonicalJson } from '../journal.js';
import { structuralLocator } from '../adapters/page-query.js';

const same = (left, right) => canonicalJson(left) === canonicalJson(right);
const exact = (value, keys) => value && typeof value === 'object' && !Array.isArray(value)
  && Object.keys(value).sort().join() === keys.sort().join();
const locatorValid = locator => Array.isArray(locator) && locator.length === 4
  && locator.every(value => typeof value === 'string');

/** Explicit host policy, never inferred from the page or a planner. Each group contains one
 * write per field followed by one approved transition. Groups and learned actions stay ordered;
 * independent DOM controls may reorder. Aliases assert reviewed meaning, not fuzzy similarity.
 */
export function createFieldGroupScope(groups, maxSteps) {
  if (!Array.isArray(groups) || !groups.length) throw Error('Field groups are required');
  const policy = structuredClone(groups), ids = new Set();
  let count = 0;
  for (const group of policy) {
    if (!exact(group, ['id', 'steps']) || typeof group.id !== 'string' || !group.id.trim()
      || ids.has(group.id) || !Array.isArray(group.steps) || group.steps.length < 2) throw Error('Invalid field group');
    ids.add(group.id);
    const aliases = new Set(), slots = new Set();
    for (const [index, step] of group.steps.entries()) {
      const isBoundary = index === group.steps.length - 1;
      if (!exact(step, isBoundary ? ['kind', 'locators'] : ['kind', 'slot', 'locators'])
        || (isBoundary ? step.kind !== 'click' : !['fill', 'select'].includes(step.kind)
          || typeof step.slot !== 'string' || !step.slot.trim() || slots.has(step.slot))
        || !Array.isArray(step.locators) || !step.locators.length || !step.locators.every(locatorValid)) {
        throw Error('Invalid ordered field-group step');
      }
      if (!isBoundary) slots.add(step.slot);
      for (const locator of step.locators) {
        const key = canonicalJson(locator);
        if (aliases.has(key) || ['submit', 'image', 'password', 'hidden', 'file'].includes(locator[3])) throw Error('Ambiguous or unsupported field-group alias');
        aliases.add(key);
      }
    }
    count += group.steps.length;
  }
  if (count > maxSteps) throw Error('Field groups exceed trace step limit');
  const at = depth => {
    let start = 0;
    for (const group of policy) {
      if (depth < start + group.steps.length) return { group, offset: depth - start };
      start += group.steps.length;
    }
    return null;
  };
  const compatible = (rule, element) => rule.locators.some(locator => same(locator, structuralLocator(element)));
  return {
    policy,
    outline(observation, depth) {
      const active = at(depth);
      if (!active) return ['terminal', observation.elements.map(structuralLocator)];
      const { group, offset } = active;
      const bound = group.steps.map(rule => {
        const matches = observation.elements.filter(element => compatible(rule, element));
        // Hidden controls also appear in workspace observations. Never silently discard duplicates.
        if (matches.length > 1) throw Error('Ambiguous field-group meaning; human review required');
        return [rule.slot ?? 'transition', matches.length === 1 ? rule.locators[0] : null];
      });
      // Reuse known fields around unrelated additions, but never cross a changed dependency
      // boundary. New controls must be independently handled before a planner may advance.
      const extras = offset === group.steps.length - 1
        ? observation.elements.filter(element => !group.steps.some(rule => compatible(rule, element))).map(structuralLocator)
        : [];
      return [group.id, bound, extras];
    },
    matches(step, element, depth, dag) {
      const active = at(depth);
      if (!active) return false;
      const { group, offset } = active;
      const isBoundary = offset === group.steps.length - 1;
      const rules = isBoundary ? group.steps.slice(-1) : group.steps.slice(0, -1);
      return rules.some(rule => rule.kind === step.kind
        && (rule.slot ? dag.hash(['slot', rule.slot]) === step.slot : step.slot === null)
        && rule.locators.some(locator => dag.hash(['locator', locator]) === step.locator)
        && compatible(rule, element));
    },
  };
}
