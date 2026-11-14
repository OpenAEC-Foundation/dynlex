import assert from 'node:assert/strict';
import {setTimeout as pause} from 'node:timers/promises';

export async function verifyFarmWeather({command,evaluate,waitFor,controls,capture}) {
  // Load a valid save immediately before the weather boundary, then use real input.
  await evaluate(`(() => {
    const key='dynlex.games.guest',progress=JSON.parse(localStorage.getItem(key));
    const world=progress.games.farm.data.world;
    world.tick=74;world.raining=false;
    localStorage.setItem(key,JSON.stringify(progress));
  })()`);
  await command('Page.reload');
  await waitFor('window.siteProbe?.paints > 2');
  await controls.clickText('Open the farm challenge →');
  await controls.clickText('One turn');
  await waitFor('window.siteProbe.texts.includes("TURN 75")');
  await controls.clickText('Whole farm');
  await waitFor('window.siteProbe.farmClouds.at(-1) === 1 && window.siteProbe.farmWeather.at(-1) > 0.5');
  const uploads=await evaluate('window.siteProbe.farmMeshUploads');
  await capture('farm-rain');
  const frames=await evaluate('window.siteProbe.farmFrames.length');
  await waitFor(`window.siteProbe.farmFrames.length > ${frames+2}`);
  assert.equal(await evaluate('window.siteProbe.farmMeshUploads'),uploads,
    'Falling rain uploaded mesh geometry during animation');
  await capture('farm-rain-moving');

  await controls.clickText('Start farm ▶');
  await waitFor('JSON.parse(localStorage.getItem("dynlex.games.guest")).games.farm.data.world.tick >= 100');
  await controls.clickText('Pause farm Ⅱ');
  assert.equal(await evaluate('JSON.parse(localStorage.getItem("dynlex.games.guest")).games.farm.data.world.raining'),false);
  await controls.clickText('Whole farm');
  await waitFor('window.siteProbe.farmClouds.at(-1) === 0 && window.siteProbe.farmWeather.at(-1) < 0.002');
  await pause(200);
  await capture('farm-after-rain');
  console.log('Farm weather crosses its real turn boundary, fades one animated cloud and returns to clear skies.');
}
