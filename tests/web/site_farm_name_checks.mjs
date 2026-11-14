import assert from 'node:assert/strict';

// Use the text drawn over the real scene; do not invoke controller callbacks.
export async function verifyFarmNames({evaluate,waitFor,controls,capture}) {
  const save='JSON.parse(localStorage.getItem("dynlex.games.guest")).games.farm.data';
  const before=await evaluate(save);
  const people=[before.world.player,...before.world.workers];
  const pointFor=name=>`(() => {
    const box=window.siteProbe.farmBounds;
    if(!box) return null;
    return window.colorProbe.find(item=>item.text===${JSON.stringify(name)} &&
      item.x>=box.x && item.x<box.x+box.width &&
      item.y>=box.y && item.y<box.y+box.height) ?? null;
  })()`;
  for(const person of people) {
    await waitFor(pointFor(person.name));
    const point=await evaluate(pointFor(person.name));
    await controls.click(point.x,point.y);
    await waitFor(`(${save}).selected === ${JSON.stringify(person.id)}`);
    assert.deepEqual((await evaluate(save)).world.player.target,before.world.player.target,
      'Clicking a person’s name also issued a terrain movement command.');
  }
  await capture('farm-person-names');
  console.log('Every Farm person has a rendered, clickable name that selects without clicking the ground.');
}
