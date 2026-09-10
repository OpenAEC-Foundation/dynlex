import assert from "node:assert/strict";
import { navigate, waitFor, evaluate, command, captureScreenshot, closeBrowserSession, runtimeExceptions } from "./browser_test_driver.mjs";

await navigate("/farm.css");
await waitFor("location.pathname === '/farm.css'", "same-origin renderer fixture");
await evaluate(`(async () => {
  const THREE = await import('/vendor/three/three.module.min.js');
  const {createFarm, relocate, moveFlag, draggableById, addFlag, removeFlag} = await import('/farm-world.js');
  const {createFarmRenderer} = await import('/farm-renderer.js');
  document.head.innerHTML='<link rel="stylesheet" href="/farm.css">';
  document.body.innerHTML='<div class="farm-game" style="width:1050px"><div class="farm-scene" style="height:790px"><div class="farm-board" data-farm-board></div></div></div>';
  window.farmFixture=createFarm();
  window.dragResults=[];window.addFlag=addFlag;window.removeFlag=removeFlag;
  window.farmView=createFarmRenderer(document.querySelector('[data-farm-board]'),farmFixture,{
    select(id){window.lastSelection=id;},
    grab(id,held){draggableById(farmFixture,id).held=held;},
    drop(id,point){dragResults.push({id,point,success:id.startsWith('flag-')?moveFlag(farmFixture,id.slice(5),point.x,point.y):relocate(farmFixture,id,point.x,point.y)});},
    walk(point){window.walkTarget=point;},key(){}
  });
  window.farmScreenPoint=(x,y,height=0,offsetX=0)=>{
    const position=new THREE.Vector3(x-9.5+offsetX,farmFixture.tiles[y][x].height+height,y-9.5).project(farmView.camera);
    const bounds=document.querySelector('canvas').getBoundingClientRect();
    return {x:bounds.left+(position.x*.5+.5)*bounds.width,y:bounds.top+(-position.y*.5+.5)*bounds.height};
  };
  window.farmClipBounds=()=>{
    const points=[];
    for(const x of [-10,10])for(const z of [-10,10])points.push(new THREE.Vector3(x,.42,z).project(farmView.camera).toArray());
    return points;
  };
})()`);
await waitFor("document.querySelector('[data-farm-board]').dataset.rendered === 'true'", "fixture rendering");
await evaluate("new Promise(resolve=>setTimeout(resolve,500))");
async function drag(fromExpression, toExpression) {
  const from = await evaluate(fromExpression), to = await evaluate(toExpression);
  await command("Input.dispatchMouseEvent", {type:"mousePressed",button:"left",clickCount:1,...from});
  await command("Input.dispatchMouseEvent", {type:"mouseMoved",buttons:1,x:from.x+(to.x-from.x)*.5,y:from.y+(to.y-from.y)*.5});
  await command("Input.dispatchMouseEvent", {type:"mouseMoved",buttons:1,...to});
  await command("Input.dispatchMouseEvent", {type:"mouseReleased",button:"left",clickCount:1,...to});
  await evaluate("new Promise(resolve=>setTimeout(resolve,400))");
}
await drag("(() => { const r=document.querySelector('[data-farm-marker=worker-1]').getBoundingClientRect(); return {x:r.x+r.width/2,y:r.y+r.height/2}; })()", "farmScreenPoint(6,12,.015)");
assert.deepEqual(await evaluate("({x:farmFixture.workers[0].x,y:farmFixture.workers[0].y})"), {x:6,y:12});
assert.equal(await evaluate("farmFixture.workers[0].held"), false);
await drag("farmScreenPoint(6,12,.55)", "farmScreenPoint(3,4,.015)");
assert.equal(await evaluate("dragResults.at(-1).success"), false);
assert.deepEqual(await evaluate("({x:farmFixture.workers[0].x,y:farmFixture.workers[0].y})"), {x:6,y:12});
await drag("farmScreenPoint(6,5,1.16,.22)", "farmScreenPoint(7,8,.015)");
assert.deepEqual(await evaluate("({x:farmFixture.flags[2].x,y:farmFixture.flags[2].y})"), {x:7,y:8});
await drag("farmScreenPoint(12,7,.27)", "farmScreenPoint(8,14,.015)");
assert.deepEqual(await evaluate("({x:farmFixture.chickens[0].x,y:farmFixture.chickens[0].y})"),{x:8,y:14});
assert.equal(await evaluate("farmFixture.chickens[0].held"),false);
await evaluate("addFlag(farmFixture,'purple');farmView.update(farmFixture)");
assert.equal(await evaluate("farmView.scene.getObjectsByProperty('type','Group').filter(g=>g.userData.entity==='flag-purple').length"),1);
await evaluate("removeFlag(farmFixture,'purple');farmView.update(farmFixture)");
assert.equal(await evaluate("farmView.scene.getObjectsByProperty('type','Group').filter(g=>g.userData.entity==='flag-purple').length"),0);
await captureScreenshot("farm-dragged-worker-and-flag");
const ground = await evaluate("farmScreenPoint(9,14,.015)");
await command("Input.dispatchMouseEvent", {type:"mousePressed",button:"left",clickCount:1,...ground});
await command("Input.dispatchMouseEvent", {type:"mouseReleased",button:"left",clickCount:1,...ground});
assert.deepEqual(await evaluate("walkTarget"), {x:9,y:14});
await evaluate("farmView.focus('farm');document.querySelector('.farm-game').style.width='100%'");
await command("Emulation.setDeviceMetricsOverride",{width:390,height:844,deviceScaleFactor:1,mobile:true});
await evaluate("new Promise(resolve=>setTimeout(resolve,900))");
for (const [x,y] of await evaluate("farmClipBounds()")) assert.ok(Math.abs(x)<.98 && Math.abs(y)<.9, "Resizing during a camera transition must still frame the entire farm");
await captureScreenshot("farm-mobile-framing");
await evaluate("farmView.dispose()");
assert.deepEqual(runtimeExceptions, []);
console.log("Real pointer dragging relocates workers, chickens and flags; removed flags leave the scene; pond drops are rejected.");
await closeBrowserSession();
