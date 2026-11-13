// Validate built assets and immutable release shells without compiling them twice.
import assert from 'node:assert/strict';
import {mkdtemp, readFile, rm, stat} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';

const root=path.resolve(import.meta.dirname,'../..');
const directory=path.resolve(process.argv[2]??path.join(root,'build/site'));
const temporary=await mkdtemp(path.join(os.tmpdir(),'dynlex-site-deployment-'));
const release='0123456789abcdef0123456789abcdef01234567';
const base=`/releases/${release}/`;
try {
  const workflow=await readFile(path.join(root,'.github/workflows/deploy.yml'),'utf8');
  assert.match(workflow,/RELEASE: \$\{\{ github\.sha \}\}/);
  assert.match(workflow,/python3 scripts\/build_site\.py --base "\/releases\/\$RELEASE\/" --output "\$RUNNER_TEMP\/site"/);
  assert.match(workflow,/mv -Tf "\$DEPLOY_PATH\/current\.next" "\$DEPLOY_PATH\/current"/);
  assert.match(workflow,/max-age=31536000, immutable/);
  assert.match(workflow,/add_header Cache-Control "no-cache" always/);
  assert.match(workflow,/-mmin \+1440/);
  const pages=JSON.parse(await readFile(path.join(directory,'routes.json'),'utf8'));
  const generated=spawnSync('python3',['-c',`
import json,sys
from pathlib import Path
root,directory,output=map(Path,sys.argv[1:4])
base=sys.argv[4]
sys.path.insert(0,str(root/'scripts'))
from build_site import write_routes,asset_base
write_routes(root/'site',output,json.loads((directory/'routes.json').read_text()),asset_base(base))
`,root,directory,temporary,base],{encoding:'utf8'});
  assert.equal(generated.status,0,generated.stderr);
  for(const page of pages) {
    const filename=path.join(temporary,page.path.slice(1));
    const html=await readFile(filename,'utf8');
    let resources=0;
    for(const tag of html.matchAll(/<(?:img|link|script|source|video|audio)\b[^>]*>/gi)) {
      for(const attribute of tag[0].matchAll(/\s(?:src|href|poster)\s*=\s*["'](.*?)["']/gi)) {
        const url=attribute[1];
        if(/^(?:[a-z][a-z0-9+.-]*:|\/\/|#)/i.test(url)) continue;
        assert(url.startsWith(base),`${filename} contains unversioned resource ${url}`);
        assert((await stat(path.join(directory,url.slice(base.length)))).isFile(),url);
        resources++;
      }
    }
    assert(resources>=2,`${filename} did not load its renderer and icon`);
  }
  const application=JSON.parse(await readFile(path.join(directory,'application.json'),'utf8'));
  for(const file of [application.module,...application.fonts,'compiler/dynlex_web.js',
    'compiler/dynlex_web.wasm','compiler/dynlex_wgsl_translator.wasm',
    'platform/start.js','packages/dynlex-ui/platform/browser/application.js']) {
    assert((await stat(path.join(directory,file))).isFile(),file);
  }
  assert.equal(await readFile(path.join(directory,'install.sh'),'utf8'),
    await readFile(path.join(root,'site/public/install.sh'),'utf8'));
  console.log(`${pages.length} release shells resolve immutable assets; compiler, UI, fonts and installer are present.`);
} finally {
  await rm(temporary,{recursive:true,force:true});
}
