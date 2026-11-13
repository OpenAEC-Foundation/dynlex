import assert from 'node:assert/strict';
import {verifySourceCallColor} from './editor_feature_checks.mjs';

// Exercise the public persistence boundary, then use the rendered UI normally.
export async function verifyFarmRestore({command, evaluate, waitFor, controls, capture}) {
  const saved = await evaluate(String.raw`(() => {
    const key = 'dynlex.games.guest';
    const progress = JSON.parse(localStorage.getItem(key));
    const data = progress.games.farm.data;
    const role = data.roles.find(role => role.id === data.roleId);
    if (!role.program) throw new Error('The applied Farm role was not saved');
    role.needsRebuild = true;
    role.source = '# A kept draft 🌽\nturn left';
    localStorage.setItem(key, JSON.stringify(progress));
    return {role:structuredClone(role), world:structuredClone(data.world)};
  })()`);
  await command('Page.reload');
  await waitFor('window.siteProbe?.paints > 2');
  await waitFor('window.siteProbe.pictures.includes("assets/games/river/painted-river.webp")');
  assert.equal(await evaluate('JSON.parse(localStorage.getItem("dynlex.games.guest")).games.farm.data.roles[0].needsRebuild'), true,
    'Reload discarded the saved role awaiting rebuilding.');
  await controls.clickText('Open the farm challenge →');
  await waitFor(`(() => {
    const data = JSON.parse(localStorage.getItem('dynlex.games.guest')).games.farm.data;
    return data.roles.every(role => !role.needsRebuild);
  })()`);
  const restored = await evaluate('JSON.parse(localStorage.getItem("dynlex.games.guest")).games.farm.data');
  const role = restored.roles.find(role => role.id === saved.role.id);
  assert.equal(role.source, saved.role.source, 'Background rebuilding replaced the newer editable draft.');
  assert.equal(role.applied, saved.role.applied, 'Background rebuilding compiled the draft instead of the applied source.');
  const withoutTraces = world => {
    for (const worker of world.workers) worker.routine.trace = null;
    return world;
  };
  assert.deepEqual(withoutTraces(restored.world), withoutTraces(saved.world),
    'Updating saved source maps moved workers or changed their execution state.');
  assert((await evaluate('window.siteProbe.compilations')).includes('farm.dl'),
    'A saved Farm role did not use the shared compiler service.');
  await controls.clickText('turn left');
  await waitFor(`document.querySelector('textarea').value === ${JSON.stringify(saved.role.source)}`);
  await verifySourceCallColor({waitFor, text:'turn'});
  await waitFor('window.siteProbe.farmFrames.length > 1');
  await waitFor('!window.siteProbe.visible.includes("Preparing the Farm…")');
  assert((await evaluate('window.siteProbe.farmDraws.at(-1)')).count > 0,
    'The restored Farm never submitted its actual scene geometry.');
  await capture('farm-restored-draft');
  console.log('A saved Farm role rebuilds its applied source without replacing a newer Unicode draft or moving workers.');
}
