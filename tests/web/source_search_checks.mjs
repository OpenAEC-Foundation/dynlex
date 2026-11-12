// Exercise the shared retained controls through the actual browser input bridge.
export async function verifySourceSearch({command,waitFor,key,capture,source,query,replacement,name}) {
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:source});
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(source)}`);
  await key('Home','Home',36,2);
  await key('KeyF','f',70,2);
  await command('Input.insertText',{text:query});
  await waitFor('window.siteProbe.visible.includes("1 of 1")');
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(query)}`);
  await key('KeyH','h',72,2);
  await command('Input.insertText',{text:replacement});
  await key('Enter','Enter',13,3); // Control+Alt+Enter replaces all in one transaction.
  await waitFor('window.siteProbe.visible.includes("No matches")');
  await capture(name+'-find-replace');
  await key('Escape','Escape',27);
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(source.replaceAll(query,replacement))}`);
  await key('KeyZ','z',90,2);
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(source)}`);
  console.log('Shared Find/Replace and atomic undo passed:',name);
}
