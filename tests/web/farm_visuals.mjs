import assert from "node:assert/strict";
import { navigate, waitFor, evaluate, captureScreenshot, closeBrowserSession, runtimeExceptions } from "./browser_test_driver.mjs";

await navigate("/farm.css");
await waitFor("location.pathname === '/farm.css'", "same-origin art fixture");
await evaluate(`(async () => {
  const {createFarm}=await import('/farm-world.js');
  const {createFarmRenderer}=await import('/farm-renderer.js');
  document.head.innerHTML='<link rel="stylesheet" href="/farm.css">';
  document.body.innerHTML='<div class="farm-game" style="width:1050px"><div class="farm-scene" style="height:790px"><div class="farm-board"></div></div></div>';
  window.farmFixture=createFarm();
  window.farmView=createFarmRenderer(document.querySelector('.farm-board'),farmFixture,{select(){},grab(){},drop(){},walk(){},key(){}});
  farmView.focus('barn',true);
})()`);
await waitFor("document.querySelector('.farm-board').dataset.rendered === 'true'", "art fixture rendering");
assert.deepEqual(await evaluate(`(() => {
  const entities={};farmView.scene.traverse(object=>{if(object.userData.entity)entities[object.userData.entity]=object;});
  return {cow:entities.cow.visible,chickens:entities.chickens.children.length};
})()`), {cow:false,chickens:2}, "Only two chickens are visible at the start");
for (const [name, offset] of [["front",[3,3.2,4]],["back",[-3,3.2,-4]],["side",[4,2,0]]]) {
  await evaluate(`farmView.camera.position.set(${offset[0] - 6.5},${offset[1] + .85},${offset[2] + 3.5});new Promise(resolve=>setTimeout(resolve,400))`);
  await captureScreenshot(`farm-roof-${name}`);
}
await evaluate(`(() => {
  Object.assign(farmFixture.player,{x:9,y:10});
  farmView.update(farmFixture,'player');farmView.focus('player',true);
  farmView.camera.position.set(2.4,4.1,5.5);
  Object.assign(farmFixture.player,{x:6,y:14});
  farmView.update(farmFixture,'player');
})()`);
await evaluate("new Promise(resolve=>setTimeout(resolve,1700))");
await captureScreenshot("farm-crop-beds");
assert.equal(await evaluate(`(async () => {
  const {deliverOrder}=await import('/farm-simulation.js');
  Object.assign(farmFixture.stock,{carrots:4,logs:2});deliverOrder(farmFixture);
  Object.assign(farmFixture.stock,{wheat:4,eggs:2});deliverOrder(farmFixture);
  farmView.update(farmFixture);
  let cow;farmView.scene.traverse(object=>{if(object.userData.entity==='cow')cow=object;});
  return cow.visible;
})()`), true, "Completing the second order reveals the cow in its pasture");
// Capture the continuous walk itself for review, and check a role tick produces
// intermediate rendered positions rather than snapping the mesh to the model.
const samples = await evaluate(`(async () => {
  const actor=farmFixture.workers[0];
  let mesh;farmView.scene.traverse(object=>{if(object.userData.entity===actor.id)mesh=object;});
  // A longer step lets software rendering sample the in-between positions too.
  const start=mesh.position.x;actor.x++;actor.direction=1;farmView.update(farmFixture,actor.id,2);
  const positions=[];
  for(let i=0;i<12;i++){await new Promise(resolve=>setTimeout(resolve,50));positions.push(mesh.position.x-start);}
  return positions;
})()`);
assert.ok(samples.some(x => x > .15 && x < .85), "Walking must render intermediate positions");
assert.ok(samples.every((x, i) => i === 0 || x >= samples[i - 1]), "Walking must not jump backward");
await evaluate("farmView.dispose()");
assert.deepEqual(runtimeExceptions, []);
console.log("Two starting chickens, the cow quest reward, roof angles, crop beds and rendered walking checked without browser exceptions.");
await closeBrowserSession();
