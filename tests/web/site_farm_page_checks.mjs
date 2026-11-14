import assert from 'node:assert/strict';
import {setTimeout as pause} from 'node:timers/promises';
import {verifyFarmNames} from './site_farm_name_checks.mjs';

// Follow rendered captions and real input; never call application callbacks.
export async function verifyFarmPage({ command, evaluate, waitFor, controls, capture }) {
  await controls.clickText('Open the farm challenge →');
  await waitFor('window.colorProbe.some(item => item.text === "Meadow Farm")');
  await controls.clickText('Whole farm');
  await waitFor('window.siteProbe.farmFrames.length > 1');
  await waitFor('!window.siteProbe.visible.includes("Preparing the Farm…")');
  assert(!(await evaluate('window.siteProbe.visible')).includes('Retry Farm view'),
    'Loaded graphics retained an irrelevant retry button.');
  const scene = await evaluate('window.siteProbe.farmDraws.at(-1)');
  assert(scene.count > 0, 'The Farm must draw the actual scene mesh instances.');
  await capture('farm-scene');
  await verifyFarmNames({evaluate,waitFor,controls,capture});

  await controls.clickText('Show Farm vocabulary');
  await controls.clickText('Checks');
  await waitFor(`window.siteProbe.visible.includes(${JSON.stringify("if it's still there:")})`);
  await capture('farm-vocabulary');
  await controls.clickText('Places');
  await waitFor('window.siteProbe.visible.includes("walk to the barn")');
  await controls.clickText('Items');
  await waitFor('window.siteProbe.visible.includes("grab an axe")');
  await controls.clickText('Hide Farm vocabulary');
  await waitFor('window.siteProbe.visible.includes("Show Farm vocabulary")');
  assert(!(await evaluate('window.siteProbe.visible')).includes('grab an axe'),
    'Collapsing the guide retained its selected content.');

  const viewport = await evaluate('({width:innerWidth,height:innerHeight,deviceScaleFactor:devicePixelRatio,mobile:false})');
  await command('Emulation.setDeviceMetricsOverride', {...viewport,width:320,height:900});
  await controls.clickText('Show Farm vocabulary');
  await controls.clickText('Actions');
  await waitFor('window.siteProbe.visible.includes("step forward")');
  await controls.clickText('Checks');
  await waitFor(`window.siteProbe.visible.includes(${JSON.stringify("if it's still there:")})`);
  await controls.clickText('Items');
  await waitFor('window.siteProbe.visible.includes("grab an axe")');
  await capture('farm-vocabulary-mobile');
  await controls.clickText('Hide Farm vocabulary');
  await command('Emulation.setDeviceMetricsOverride', viewport);

  await controls.wheel(100000);
  await waitFor('!window.siteProbe.farmBounds');
  await pause(1200);
  const idle = await evaluate('window.siteProbe.paints');
  await pause(800);
  assert.equal(await evaluate('window.siteProbe.paints'), idle,
    'Farm kept repainting after its scene scrolled out of view.');
  await capture('farm-offscreen-idle');

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
  await waitFor('window.siteProbe.farmBounds !== null');
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
