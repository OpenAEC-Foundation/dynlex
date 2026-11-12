import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';
const {output,requests}=await recordWebsiteProgram(process.argv[2]);
assert.equal(output,'Website Run shortcuts use shared scoped commands, the matching control and its enabled policy.\n');
assert.equal(requests.length,0);
console.log('Website shared Run command tests passed.');
