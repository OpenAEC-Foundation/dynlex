// This snapshot was captured from the existing commands before their DynLex port.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vocabulary from '../../web/farm-vocabulary.json' with {type: 'json'};
import {ACTIONS, CONDITIONS, MANUAL_ACTIONS, action, condition, example} from '../../web/farm-vocabulary.js';
const expected = JSON.parse(await fs.readFile(new URL('./fixtures/farm-vocabulary.json', import.meta.url)));
assert.deepEqual(vocabulary, expected);
for (const [kind, entries, make] of [['actions', ACTIONS, action], ['conditions', CONDITIONS, condition]]) {
  entries.forEach((entry, index) => {
    assert.equal(example(entry), expected.EXAMPLES[kind][index]);
    const instruction = make(entry.key);
    assert.deepEqual([instruction.argument, instruction.amount, instruction.target],
      [entry.defaults.argument, entry.defaults.amount, entry.defaults.target]);
  });
}
assert.equal(MANUAL_ACTIONS.length, 20);
console.log('All Farm commands, toolbar instructions, defaults and help examples match their independent snapshot.');
