// Application vocabulary, toolbar choices and help text are authored in DynLex.
import vocabulary from "./farm-vocabulary.json" with {type: "json"};
export const {FLAGS, ITEMS, DESTINATIONS, SUBJECT_TARGET, NO_TARGET, ALL_ITEMS, SUBJECT_ITEMS,
  DIRECTIONS, FEATURES, ROLE_NAMES, ITEM_ALIASES, TARGET_ALIASES, ACTIONS, CONDITIONS, MANUAL_ACTIONS} = vocabulary;

export const targetId = key => DESTINATIONS.find(entry => entry.key === key).id;
export const itemId = key => ITEMS.find(entry => entry.key === key).id;
export const itemInfo = key => ITEMS.find(item => item.key === key);
export const actionInfo = key => ACTIONS.find(action => action.key === key);
export function instruction(op, key, values = {}) {
  const entry = (op === "action" ? ACTIONS : CONDITIONS).find(candidate => candidate.key === key);
  return {op, key, ...entry.defaults, area: "", range: null, ...values};
}
export const action = (key, values) => instruction("action", key, values);
export const condition = (key, values) => instruction("test", key, values);
export function example(entry) {
  return (ACTIONS.includes(entry) ? vocabulary.EXAMPLES.actions : vocabulary.EXAMPLES.conditions)[entry.id];
}
