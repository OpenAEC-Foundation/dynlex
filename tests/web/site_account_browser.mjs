import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {setTimeout as pause} from 'node:timers/promises';
import {command,evaluate,waitFor,navigate,dispatchKey,closeBrowserSession,runtimeExceptions,consoleErrors} from './browser_test_driver.mjs';

const progress=source=>({version:7,games:{river:{updatedAt:10,data:{source,solved:false,coding:{
  passenger:{count:0,seen:[]},deliveries:{count:0,seen:[]},rescue:{count:0,seen:[]}
}}}},lastGame:'river'});
const guest=JSON.stringify(progress('guest browser plan'));
const visible=text=>`window.textProbe.some(item=>item.text===${JSON.stringify(text)})`;
async function revealText(text,header=false) {
  for(let attempt=0;attempt<80;attempt++) {
    const point=await evaluate(`window.textProbe.find(item=>item.text===${JSON.stringify(text)}&&item.y>${header?0:80}&&item.y<innerHeight-20)`);
    if(point) {
      const settled=await evaluate(`(async()=>{
        let previous,stable=0;
        for(let frame=0;frame<180;frame++) {
          await new Promise(requestAnimationFrame);
          const current=window.textProbe.find(item=>item.text===${JSON.stringify(text)}&&item.y>${header?0:80}&&item.y<innerHeight-20);
          if(!current) return null;
          stable=previous&&Math.abs(previous.x-current.x)<0.1&&Math.abs(previous.y-current.y)<0.1?stable+1:0;
          if(stable>=3) return current;
          previous=current;
        }
        throw new Error('The caption did not settle: '+${JSON.stringify(text)});
      })()`);
      if(settled) return settled;
    }
    const cursor={x:1420,y:700};
    await command('Input.dispatchMouseEvent',{type:'mouseMoved',...cursor});
    await command('Input.dispatchMouseEvent',{type:'mouseWheel',...cursor,deltaX:0,deltaY:attempt===0?-100000:400});
    await pause(250);
  }
  throw new Error('Could not reveal '+text+'; visible: '+await evaluate('JSON.stringify(window.textProbe.map(item=>item.text))'));
}
async function clickText(text,header=false) {
  const point=await revealText(text,header);
  await command('Input.dispatchMouseEvent',{type:'mouseMoved',x:point.x,y:point.y});
  await command('Input.dispatchMouseEvent',{type:'mousePressed',x:point.x,y:point.y,button:'left',clickCount:1});
  await evaluate('new Promise(requestAnimationFrame)');
  await command('Input.dispatchMouseEvent',{type:'mouseReleased',x:point.x,y:point.y,button:'left',clickCount:1});
  await pause(150);
}
async function edit(text,value) {
  await clickText(text);await dispatchKey('a','KeyA',65,2);
  await command('Input.insertText',{text:value});await pause(150);
}
async function remote(source) {
  await evaluate(`(async()=>{
    const {auth,store,storeAPI}=window.accountTest;
    await storeAPI.setDoc(storeAPI.doc(store,'players',auth.currentUser.uid,'games','progress'),
      {version:7,payload:${JSON.stringify(JSON.stringify(progress(source)))},updatedAt:storeAPI.serverTimestamp()});
  })()`);
}
const snapshot=()=>evaluate('JSON.parse(localStorage.getItem("dynlex.games."+localStorage.getItem("dynlex.games.profile")))');
async function capture(name) {
  const screenshot=await command('Page.captureScreenshot',{format:'png'});
  await fs.writeFile('.cache/account-browser-'+name+'.png',Buffer.from(screenshot.data,'base64'));
}
try {
  await command('Runtime.enable');await command('Page.enable');await command('Network.enable');
  await command('Emulation.setDeviceMetricsOverride',{width:1440,height:1000,deviceScaleFactor:1,mobile:false});
  await command('Page.addScriptToEvaluateOnNewDocument',{source:await fs.readFile(new URL('./fixtures/ui-text-probe.js',import.meta.url),'utf8')});
  await command('Page.addScriptToEvaluateOnNewDocument',{source:`localStorage.setItem('dynlex.games.guest',${JSON.stringify(guest)});`});
  await navigate('/#challenges');
  await waitFor(visible('Sign in with Google'),'the DynLex account controls');
  await capture('signed-out');
  await clickText('IDE ↗',true);
  await waitFor(visible('import lib/std.dl'),'the IDE starter');
  assert.equal(await evaluate('new URL(document.querySelector("link[rel=icon]").href).pathname'),'/assets/branding/dynlex-icon.svg');
  await edit('import lib/std.dl','unsaved IDE browser draft');
  await clickText('Challenges',true);
  await clickText('Sign in with Google');
  await waitFor(visible('Progress saved to your account'),'guest import and its cloud write');
  const alice=await evaluate('window.accountTest.auth.currentUser.uid');
  assert.equal(await evaluate('localStorage.getItem("dynlex.games.profile")'),'user:'+alice);
  await clickText('Open the river challenge →');
  await edit('guest browser plan','account unsynced');
  await remote('remote device');
  await evaluate('window.dispatchEvent(new Event("online"))');
  await waitFor(visible('Load account save'),'the concurrent-device conflict');
  await capture('conflict');
  await clickText('Load account save');
  await waitFor('JSON.parse(localStorage.getItem("dynlex.games."+localStorage.getItem("dynlex.games.profile"))).games.river.data.source==="remote device"','loading the remote game');
  await revealText('Progress saved to your account');
  assert.equal((await snapshot()).games.river.data.source,'remote device');
  assert.equal(await evaluate(`JSON.parse(localStorage.getItem('dynlex.games.backup:'+${JSON.stringify(alice)})).games.river.data.source`),'account unsynced');
  await edit('remote device','keep local');
  await remote('remote two');
  await evaluate('window.dispatchEvent(new Event("online"))');
  await waitFor(visible('Keep this game'),'the second conflict');
  await clickText('Keep this game');
  await revealText('Progress saved to your account');
  const cloud=await evaluate(`(async()=>{
    const {store,storeAPI}=window.accountTest;
    return JSON.parse((await storeAPI.getDocFromServer(storeAPI.doc(store,'players',${JSON.stringify(alice)},'games','progress'))).data().payload);
  })()`);
  assert.equal(cloud.games.river.data.source,'keep local');
  await clickText('Sign out');
  await waitFor(visible('Sign in with Google'),'the signed-out account observer');
  assert.equal(await evaluate('localStorage.getItem("dynlex.games.profile")'),'guest');
  assert.equal(await evaluate('localStorage.getItem("dynlex.games.guest")'),guest);
  await clickText('guest browser plan');
  await clickText('IDE ↗',true);
  await waitFor(visible('unsaved IDE browser draft'),'the preserved IDE draft');
  assert.equal(runtimeExceptions.length,0,JSON.stringify(runtimeExceptions));
  assert.equal(consoleErrors.length,0,JSON.stringify(consoleErrors));
  console.log('Real browser account UI passed: guest import, Google identity, conflict backup/load/keep, cloud persistence, sign-out, retained guest and unsaved IDE draft.');
} catch(error) {
  console.error(await evaluate('JSON.stringify({visible:window.textProbe.map(item=>item.text),account:window.serviceProbe.filter(item=>item.service==="firebase").map(item=>item.payload.operation)})'));
  throw error;
} finally {
  const screenshot=await command('Page.captureScreenshot',{format:'png'});
  await fs.writeFile('.cache/account-browser.png',Buffer.from(screenshot.data,'base64'));
  await closeBrowserSession();
}
