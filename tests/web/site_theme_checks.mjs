import assert from 'node:assert/strict';
import {verifySourceCallColor} from './editor_feature_checks.mjs';

// Use the same pointer, keyboard and storage operations as the real site.
export async function verifyIdeTheme({command,evaluate,waitFor,controls,key,capture,source}) {
  const savedSource=async()=>{
    const count=await evaluate('window.siteProbe.downloads.length');
    await controls.clickText('Save source ↓');
    await waitFor(`window.siteProbe.downloads.length > ${count} && window.siteProbe.downloads[${count}].content !== undefined`);
    return evaluate(`window.siteProbe.downloads[${count}].content`);
  };
  await controls.clickText('Switch to light');
  await waitFor('localStorage.getItem("dynlex-web-theme") === "light"');
  await verifySourceCallColor({waitFor,text:'print line',theme:'light'});
  assert.equal(await savedSource(),source,'Switching theme replaced the editor draft');
  await capture('ide-light');

  await controls.clickVisibleText('import lib/std.dl');
  await key('End','End',35,2);
  await command('Input.insertText',{text:'\n# theme round trip'});
  await controls.clickText('Switch to dark');
  await waitFor('localStorage.getItem("dynlex-web-theme") === "dark"');
  await verifySourceCallColor({waitFor,text:'print line'});
  assert.equal(await savedSource(),source+'\n# theme round trip','Changing theme discarded a newer edit');
  await controls.clickVisibleText('import lib/std.dl');
  await key('KeyZ','z',90,2);
  assert.equal(await savedSource(),source,'Changing theme lost the editor undo history');
  await capture('ide-dark');

  await controls.clickText('Switch to light');
  await waitFor('localStorage.getItem("dynlex-web-theme") === "light"');
  await command('Page.reload',{ignoreCache:true});
  await waitFor('window.colorProbe?.some(item=>item.text.trim()==="Switch to dark")');
  await verifySourceCallColor({waitFor,text:'print line',theme:'light'});
  await capture('ide-light-reloaded');
  await controls.clickText('Switch to dark');
  await waitFor('localStorage.getItem("dynlex-web-theme") === "dark"');
  await verifySourceCallColor({waitFor,text:'print line'});
  console.log('IDE themes use canonical colors, preserve live drafts and undo, and persist through reload.');
}
