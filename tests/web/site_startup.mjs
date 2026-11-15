import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';

const result=await recordWebsiteProgram(process.argv[2],{allocationLimit:32*1024*1024});
assert.equal(result.output,'Website startup applies environment preferences before the initial route, navigation watch and graphics loading.\n');
assert.deepEqual(result.requests.map(request=>request.service),['environment','document','location','fetch']);
assert.deepEqual(result.requests[0].payload,null);
assert.equal(result.requests[1].payload.path,'/river-character-sheet.html');
assert.deepEqual(result.requests[2].payload,{action:'watch'});
assert.deepEqual(result.requests[3].payload,{url:'assets/shaders/index.json'});
assert.equal(result.allocations.live,0,'Startup retained its environment, session or pending requests');
console.log('Environment, initial-route preferences and service dependency ordering passed.');
