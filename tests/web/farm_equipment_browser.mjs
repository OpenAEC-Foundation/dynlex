import assert from "node:assert/strict";
import { navigate, waitFor, evaluate, captureScreenshot, closeBrowserSession, runtimeExceptions } from "./browser_test_driver.mjs";
await navigate('/farm.css');
await waitFor("location.pathname==='/farm.css'",'fixture');
await evaluate(`(async()=>{
 const {createFarm}=await import('/farm-world.js');
 const {createFarmRenderer}=await import('/farm-renderer.js');
 document.head.innerHTML='<link rel="stylesheet" href="/farm.css">';
 document.body.innerHTML='<div class="farm-game" style="width:900px"><div class="farm-scene" style="height:700px"><div class="farm-board"></div></div></div>';
 window.world=createFarm();Object.assign(world.player,{x:8,y:14,direction:2});
 window.view=createFarmRenderer(document.querySelector('.farm-board'),world,{select(){},grab(){},drop(){},walk(){},key(){}});
 view.focus('player',true);
 view.camera.position.set(-.2,2.1,8.7);
})()`);
await waitFor("document.querySelector('.farm-board').dataset.rendered==='true'",'render');
for(const [name,items] of [['axe-logs',['axe','logs']],['hoe-bucket',['hoe','bucket']],['shovel-seeds',['shovel','seeds']],['bucket-axe',['bucket','axe']]]) {
 await evaluate(`world.player.inventory=${JSON.stringify(items.map(kind=>({kind,count:1})))};view.update(world,'player');new Promise(resolve=>setTimeout(resolve,1200))`);
 assert.deepEqual(await evaluate(`(()=>{let farmer;view.scene.traverse(o=>{if(o.userData.entity==='player')farmer=o;});return farmer.userData.hands.map(hand=>hand.children.map(item=>item.name));})()`),items.map(item=>[item]));
 await captureScreenshot('farm-grip-'+name);
}
await evaluate(`world.player.status='Chopping…';view.update(world);new Promise(resolve=>setTimeout(resolve,500))`);
await captureScreenshot('farm-grip-chopping');
const rain = await evaluate(`(async()=>{
 const rain=view.scene.getObjectByName('rain');
 world.raining=true;view.update(world);const start=rain.material.opacity;
 await new Promise(r=>setTimeout(r,700));const during=rain.material.opacity;
 world.raining=false;view.update(world);const stop=rain.material.opacity;
 await new Promise(r=>setTimeout(r,900));const fading=rain.material.opacity;
 return {start,during,stop,fading};
})()`);
assert.equal(rain.start,0);assert.ok(rain.during>0 && rain.during<.6);
assert.equal(rain.stop,rain.during);assert.ok(rain.fading>0 && rain.fading<rain.stop);
await evaluate(`(async()=>{
 const {deliverOrder,tickFarm}=await import('/farm-simulation.js');
 Object.assign(world.stock,{carrots:4,logs:2});deliverOrder(world);
 Object.assign(world.stock,{wheat:4,eggs:2});deliverOrder(world);
 world.player.status='Waiting.';world.player.inventory=[];
 for(let i=0;i<8;i++) tickFarm(world,[]);
 view.update(world);view.focus('cow',true);view.camera.position.set(8,10,9);
 await new Promise(r=>setTimeout(r,2000));
})()`);
await captureScreenshot('farm-enclosed-pens');
const motion = await evaluate(`(async()=>{
 const {tickFarm}=await import('/farm-simulation.js');
 const animal=world.chickens[0];let mesh;view.scene.traverse(o=>{if(o.userData.entity===animal.id)mesh=o;});
 const start={x:animal.x,y:animal.y};
 while(animal.x===start.x && animal.y===start.y) tickFarm(world,[]);
 view.update(world,'player',2);
 const distances=[];
 for(let i=0;i<12;i++) {await new Promise(r=>setTimeout(r,100));distances.push(Math.hypot(mesh.position.x-(start.x-9.5),mesh.position.z-(start.y-9.5)));}
 return distances;
})()`);
assert.ok(motion.some(distance=>distance>.05 && distance<.95),'Animals render intermediate walking positions');
await evaluate('view.dispose()');
assert.deepEqual(runtimeExceptions,[]);
console.log('Rendered hand attachments, both tool slots, closed pens and gradual rain checked.');
await closeBrowserSession();
