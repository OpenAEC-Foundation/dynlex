import assert from 'node:assert/strict';
import {setTimeout as pause} from 'node:timers/promises';

// Observe the painted controls and use normal input, without application hooks.
export async function verifyRiverInspection({command,evaluate,waitFor,controls,capture}) {
  const visible=text=>`window.colorProbe.some(item=>item.text===${JSON.stringify(text)})`;
  const frameValue=`(() => {
    const caption=window.colorProbe.find(item=>item.text==='Inspection frame');
    if(!caption) return null;
    const value=window.colorProbe.find(item=>/^\\d+(?:\\.\\d+)?$/.test(item.text)
      && item.x>caption.x && Math.abs(item.y-caption.y)<3);
    return value?Number(value.text):null;
  })()`;
  const expectFrame=value=>waitFor(`${frameValue}===${value}`);
  const idle=async()=>{
    let previous=-1,stable=0;
    for(let sample=0;sample<40 && stable<6;sample++) {
      await pause(200);
      const paints=await evaluate('window.siteProbe.paints');
      stable=paints===previous?stable+1:0;
      previous=paints;
    }
    assert(stable>=6,'The paused page did not settle after loading');
    const paints=await evaluate('window.siteProbe.paints');
    await pause(800);
    assert.equal(await evaluate('window.siteProbe.paints'),paints,
      'Paused character portraits kept repainting');
  };
  const art=async()=>{
    const clip=await evaluate(`(() => {
      const heading=window.colorProbe.find(item=>item.text==='Farmer + boat');
      if(!heading) throw Error('The boat portraits are outside the viewport');
      const y=Math.ceil(heading.y+heading.height/2+24);
      return {x:0,y,width:innerWidth,height:innerHeight-y,scale:1};
    })()`);
    return (await command('Page.captureScreenshot',{format:'png',clip})).data;
  };

  await waitFor('document.title==="River character animation sheet · DynLex"');
  await controls.clickText('Pause loops');
  await waitFor(visible('Play loops'));
  await controls.clickText('Inspection frame');
  await controls.key('Home','Home',36);
  await expectFrame(0);
  await controls.key('End','End',35);
  await expectFrame(100);
  await controls.key('ArrowLeft','ArrowLeft',37);
  await expectFrame(99);
  await controls.key('Home','Home',36);
  for(let index=0;index<5;index++) await controls.key('PageUp','PageUp',33);
  await expectFrame(50);
  await idle();
  const plain=await art();
  await controls.clickText('Show overlay bounds');
  await idle();
  assert.notEqual(await art(),plain,'Overlay bounds did not change the character artwork');
  await capture('river-character-bounds');
  await controls.clickText('Show overlay bounds');
  await idle();
  assert.equal(await art(),plain,'Hiding overlay bounds changed the paused artwork');
  await capture('river-character-paused');

  await controls.clickText('Bellowing');
  await capture('river-character-sheep');
  await controls.clickText('Licking');
  await waitFor('window.siteProbe.pictures.includes("assets/games/river/wolf-tongue.svg")');
  await waitFor('window.siteProbe.pictures.includes("assets/games/river/wolf-mouth-guide.svg")');
  await idle();
  await capture('river-character-wolf');
  await controls.clickText('The hay has no face or idle animation.');
  await waitFor('window.siteProbe.pictures.includes("assets/games/river/hay.webp")');
  await capture('river-character-hay');

  await controls.clickText('Play loops');
  const playing=await evaluate('window.siteProbe.paints');
  await pause(500);
  assert((await evaluate('window.siteProbe.paints'))>playing,'Playing loops did not advance');
  const docs=await evaluate('window.colorProbe.find(item=>item.text==="Docs")');
  assert(docs,'The retained site navigation disappeared');
  await controls.click(docs.x,docs.y);
  await waitFor('location.pathname==="/wiki/index.html"');
  await idle();
  await evaluate('history.back()');
  await waitFor('location.pathname==="/river-character-sheet.html"');
  await waitFor(visible('Pause loops'));
  await controls.clickText('Pause loops');

  await command('Emulation.setEmulatedMedia',{features:[{name:'prefers-reduced-motion',value:'reduce'}]});
  await command('Page.reload',{ignoreCache:true});
  await waitFor('window.colorProbe?.some(item=>item.text==="Play loops")');
  await idle();
  await controls.clickText('Restart loops');
  await waitFor(visible('Play loops'));
  await expectFrame(0);
  await idle();
  await command('Emulation.setDeviceMetricsOverride',{width:375,height:900,deviceScaleFactor:2,mobile:false});
  await controls.clickText('Inspection frame');
  await controls.key('End','End',35);
  await expectFrame(100);
  await idle();
  await capture('river-character-mobile');
  console.log('River portraits share production art, range input, debug overlays, pause/resume, navigation and reduced motion.');
}
