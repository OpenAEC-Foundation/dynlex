import assert from 'node:assert/strict';

// Follow rendered captions and real input; never call application callbacks.
export async function verifyFarmPage({ command, evaluate, waitFor, controls, capture }) {
  await controls.clickText('Open the farm challenge →');
  await waitFor('window.colorProbe.some(item => item.text === "Meadow Farm")');
  await controls.clickText('Whole farm');
  await waitFor('window.siteProbe.farmFrames.length > 1');
  await waitFor('!window.siteProbe.visible.includes("Preparing the Farm…")');
  const scene = await evaluate('window.siteProbe.farmDraws.at(-1)');
  assert(scene.count > 0, 'The Farm must draw the actual scene mesh instances.');
  await capture('farm-scene');

  const turnPoint=await controls.clickText('One turn');
  console.log('Farm turn input:',JSON.stringify({point:turnPoint,after:await evaluate('window.colorProbe.find(item => item.text === "One turn")')}));
  await waitFor('window.siteProbe.texts.includes("TURN 1")');
  await controls.clickText('Start farm ▶');
  await waitFor('window.siteProbe.texts.includes("Pause farm Ⅱ")');
  await waitFor('window.siteProbe.texts.some(text => /^TURN [2-9][0-9]*$/.test(text))');
  await controls.clickText('Pause farm Ⅱ');
  await controls.clickText('Reset farm');
  await waitFor('window.siteProbe.visible.includes("TURN 0")');

  await evaluate('window.siteProbe.texts.length = 0');
  await controls.clickText('Give this role to Ada');
  await waitFor('window.siteProbe.texts.some(text => text.startsWith("Role applied."))');
  await waitFor('window.siteProbe.texts.includes("1 worker using this code")');
  assert((await evaluate('window.siteProbe.compilations')).includes('farm.dl'),
    'Farm roles must run through the same compiler service as the IDE.');
  await capture('farm-role');
  await controls.clickText('Pause farm Ⅱ');
  await controls.clickText('Whole farm');
  const before = await evaluate('window.siteProbe.farmFrames.length');
  const point = await evaluate(`(() => {
    const box = window.siteProbe.farmBounds;
    return {x:box.x+box.width/2,y:box.y+box.height/2};
  })()`);
  await command('Input.dispatchMouseEvent', { type: 'mouseMoved', ...point });
  await command('Input.dispatchMouseEvent', { type: 'mousePressed', ...point, button: 'right', buttons: 2, clickCount: 1 });
  await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x: point.x + 80, y: point.y + 20, buttons: 2 });
  await command('Input.dispatchMouseEvent', { type: 'mouseReleased', x: point.x + 80, y: point.y + 20, button: 'right', buttons: 0, clickCount: 1 });
  await waitFor(`window.siteProbe.farmFrames.length > ${before}`);
  await capture('farm-orbit');
  await controls.clickText('← Choose a challenge');
  await waitFor('window.colorProbe.some(item => item.text === "Open the farm challenge →")');
  console.log('Farm opens from its real link, renders, advances, resets, applies a role, orbits, and returns to the gallery.');
}
