import { installEditorAccess, withEditor, editCode, readCode } from "./editor_test_driver.mjs";
import assert from "node:assert/strict";
import { assertRiverEnterCommitsLine } from "./river_completion_browser.mjs";
import { assertRiverIncrementalHighlighting } from "./river_highlighting_browser.mjs";
import { aboardSpriteBounds } from "./river_sprite_browser.mjs";

const previewAssets = [
  "/river-challenge.css",
  "/media/river-challenge/painted-river.webp",
  "/media/river-challenge/sheep.webp",
  "/media/river-challenge/sheep-blink.webp",
  "/media/river-challenge/wolf.webp",
  "/media/river-challenge/wolf-blink.webp",
  "/media/river-challenge/hay.webp",
  "/media/river-challenge/boat.webp",
  "/media/river-challenge/boat-blink.webp",
  "/media/river-challenge/speech-sprig.svg"
];

const runtimeAssets = [
  "/river-challenge.js",
  "/river-character-art.js",
  "/river-program.js",
  "/river-challenge-audio.js",
  "/river-challenge-model.js",
  "/media/river-challenge/puzzle-casual-game-music.mp3",
  "/media/river-challenge/forest-river-ambience-loop.mp3",
  "/media/river-challenge/boat-whoosh.mp3",
  "/media/river-challenge/sheep-idle.mp3",
  "/media/river-challenge/sheep-anxious.mp3",
  "/media/river-challenge/rowing-paddle.mp3",
  "/media/river-challenge/win-level-up.mp3"
];

export function assertRiverChallengeLoadingBoundary(requestedUrls) {
  for (const asset of previewAssets) {
    assert.equal(
      requestedUrls.some((url) => url.includes(asset)),
      true,
      `The river challenge preview must request ${asset}`
    );
  }
  for (const asset of runtimeAssets) {
    assert.equal(
      requestedUrls.some((url) => url.includes(asset)),
      false,
      `The river challenge runtime asset ${asset} must stay lazy before acceptance`
    );
  }
}

export async function runRiverChallengeBrowserTest({
  captureScreenshot,
  clickElement,
  dispatchKey,
  evaluate,
  requestedUrls,
  waitFor
}) {
  const starterSource = "# The official names are: sheep, wolf, and hay.\n"
    + "get the hay in the boat\n"
    + "row to the other side";
  const preview = await evaluate(`(() => {
    const stage = document.querySelector('[data-river-preview] [data-river-stage]');
    const button = document.querySelector('[data-river-challenge-load]');
    const bubble = button.closest('.river-speech');
    const bubbleStyle = getComputedStyle(bubble);
    const ornamentStyle = getComputedStyle(bubble, '::before');
    const tailStyle = getComputedStyle(bubble, '::after');
    const boatElement = stage.querySelector('[data-river-boat]');
    const hullElement = stage.querySelector('[data-river-boat-hull]');
    const boat = boatElement.getBoundingClientRect();
    const hull = hullElement.getBoundingClientRect();
    const bubbleRect = bubble.getBoundingClientRect();
    const stageRect = stage.getBoundingClientRect();
    const farmerHeadX = boat.left + boat.width * 0.36;
    const tailCenterX = bubbleRect.left + parseFloat(tailStyle.left)
      + parseFloat(tailStyle.width) / 2;
    return {
      stageVisible: stage.getBoundingClientRect().height > 0,
      characters: stage.querySelectorAll('[data-river-character]').length,
      standaloneFarmers: stage.querySelectorAll('[data-river-character="FARMER"]').length,
      compositeFarmer: boatElement.querySelector('[data-river-boat-farmer]') !== null,
      hullLayers: stage.querySelectorAll('[data-river-boat-hull]').length,
      hullAlignedWithBoat: (
        Math.abs(hull.left - boat.left) <= 0.5
        && Math.abs(hull.top - boat.top) <= 0.5
        && Math.abs(hull.width - boat.width) <= 0.5
        && Math.abs(hull.height - boat.height) <= 0.5
      ),
      speechTailHorizontalOffset: Math.abs(tailCenterX - farmerHeadX),
      speechVerticalGap: boat.top - bubbleRect.bottom,
      boatHeight: boat.height,
      speechNearFarmerVertically: (
        bubbleRect.bottom >= boat.top - boat.height * 0.75
        && bubbleRect.bottom <= boat.top + boat.height * 0.12
      ),
      speech: bubble.querySelector('span').textContent,
      action: button.textContent.trim(),
      trailerPresent: document.querySelector('.challenge-door') !== null,
      bubbleBottomLeftRadius: parseFloat(bubbleStyle.borderBottomLeftRadius),
      bubbleBackground: bubbleStyle.backgroundImage,
      ornamentBackground: ornamentStyle.backgroundImage,
      ornamentBottomRelativeToBubble: (
        parseFloat(ornamentStyle.top) + parseFloat(ornamentStyle.height)
      ),
      ornamentInsideScene: (
        bubbleRect.top + parseFloat(ornamentStyle.top) >= stageRect.top
      )
    };
  })()`);
  assert.equal(preview.stageVisible, true);
  assert.equal(preview.characters, 3);
  assert.equal(preview.standaloneFarmers, 0);
  assert.equal(preview.compositeFarmer, true);
  assert.equal(preview.hullLayers, 1);
  assert.equal(preview.hullAlignedWithBoat, true);
  assert.ok(
    preview.speechTailHorizontalOffset <= 25,
    `Opening speech tail is ${preview.speechTailHorizontalOffset}px from the farmer`
  );
  assert.equal(
    preview.speechNearFarmerVertically,
    true,
    `Opening speech vertical gap is ${preview.speechVerticalGap}px for a ${preview.boatHeight}px boat`
  );
  assert.equal(preview.speech, "Can you help me cross the river?");
  assert.equal(preview.action, "OKAY");
  assert.equal(preview.trailerPresent, false);
  assert.ok(preview.bubbleBottomLeftRadius >= 18);
  assert.notEqual(preview.bubbleBackground, "none");
  assert.notEqual(preview.ornamentBackground, "none");
  assert.ok(preview.ornamentBottomRelativeToBubble <= 5);
  assert.equal(preview.ornamentInsideScene, true);
  const ornamentPadding = await evaluate(`(async () => {
    const bubble = document.querySelector('[data-river-preview-speech]');
    const background = getComputedStyle(bubble, '::before').backgroundImage;
    if (!background.startsWith('url("') || !background.endsWith('")')) {
      throw new Error('Speech ornament must use its complete image asset');
    }
    const image = new Image();
    image.src = background.slice(5, -2);
    await image.decode();
    const canvas = document.createElement('canvas');
    canvas.width = image.naturalWidth;
    canvas.height = image.naturalHeight;
    const context = canvas.getContext('2d');
    context.drawImage(image, 0, 0);
    const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let left = canvas.width;
    let right = -1;
    for (let y = 0; y < canvas.height; y += 1) {
      for (let x = 0; x < canvas.width; x += 1) {
        if (pixels[(y * canvas.width + x) * 4 + 3] === 0) continue;
        left = Math.min(left, x);
        right = Math.max(right, x);
      }
    }
    if (right < left) throw new Error('Speech ornament is empty');
    return { left, right: canvas.width - right - 1 };
  })()`);
  assert.ok(ornamentPadding.left >= 2);
  assert.ok(ornamentPadding.right >= 2);
  await evaluate(
    "document.querySelector('[data-river-preview]')"
      + ".scrollIntoView({ block: 'center', behavior: 'instant' })"
  );
  await captureScreenshot("homepage-challenge-preview");
  await evaluate(`(() => {
    window.__riverAudioStarts = [];
    window.__riverTongueAnimations = [];
    const originalConnect = AudioBufferSourceNode.prototype.connect;
    const originalStart = AudioBufferSourceNode.prototype.start;
    const originalAnimate = Element.prototype.animate;
    AudioBufferSourceNode.prototype.connect = function(destination, ...rest) {
      this.__riverCueVolume = destination?.gain?.value;
      return originalConnect.call(this, destination, ...rest);
    };
    AudioBufferSourceNode.prototype.start = function(...args) {
      window.__riverAudioStarts.push({
        duration: this.buffer.duration,
        loop: this.loop,
        playbackRate: this.playbackRate.value,
        volume: this.__riverCueVolume
      });
      return originalStart.apply(this, args);
    };
    Element.prototype.animate = function(keyframes, options) {
      if (this.matches?.('.river-wolf-tongue')) {
        const frames = Array.from(keyframes);
        window.__riverTongueAnimations.push({
          duration: options.duration,
          iterations: options.iterations ?? 1,
          firstLeft: Number.parseFloat(frames.at(0).left),
          lastLeft: Number.parseFloat(frames.at(-1).left),
          fighting: this.closest('.river-wolf').classList.contains('is-fighting')
        });
      }
      return originalAnimate.call(this, keyframes, options);
    };
  })()`);
  await installEditorAccess();
  await clickElement("[data-river-challenge-load]");
  await waitFor(
    "document.querySelector('[data-river-challenge-mount]').dataset.challengeLoaded === 'true'",
    "the river challenge to load after its explicit click"
  );
  for (const asset of runtimeAssets) {
    assert.ok(
      requestedUrls.some((url) => url.includes(asset)),
      `Opening the river challenge must request ${asset}`
    );
  }
  assert.equal(
    await evaluate("editorFor('[data-river-editor-shell]').getValue()"),
    starterSource
  );
  assert.equal(
    await evaluate("document.querySelector('[data-river-preview]')"),
    null
  );
  assert.equal(
    await evaluate("document.querySelector('[data-river-speech]').hidden"),
    true
  );
  await waitFor(
    "window.__riverAudioStarts.some((start) => start.loop"
      + " && start.duration >= 29 && start.duration <= 31"
      + " && Math.abs(start.volume - 0.28) < 0.001)",
    "the compact forest-river recording to start as a continuous ambience loop"
  );
  await assertRiverIncrementalHighlighting({ evaluate, starterSource, waitFor });
  await withEditor('[data-river-editor-shell]', `
    const line=model.getLineCount();
    editor.focus();editor.setPosition({lineNumber:line,column:3});
    editor.executeEdits('test',[{range:{startLineNumber:line,startColumn:1,endLineNumber:line,endColumn:model.getLineMaxColumn(line)},text:'ro'}]);
    editor.setPosition({lineNumber:line,column:3});editor.trigger('test','editor.action.triggerSuggest',{});
  `);
  await waitFor("[...document.querySelectorAll('.suggest-widget.visible .monaco-list-row')].some(row=>row.textContent.includes('row'))",'river row completion');
  await dispatchKey('Tab','Tab',9);
  assert.match(await readCode('[data-river-editor-shell]'),/\nrow /);
  await assertRiverEnterCommitsLine({ dispatchKey, evaluate, waitFor });
  await editCode('[data-river-editor-shell]',starterSource);
  const presentation = await evaluate(`(() => {
    const stage = document.querySelector('[data-river-stage]');
    const boatElement = document.querySelector('[data-river-boat]');
    const hullElement = document.querySelector('[data-river-boat-hull]');
    const boatImage = boatElement.querySelector('img');
    const boat = boatElement.getBoundingClientRect();
    const hull = hullElement.getBoundingClientRect();
    const farmerBlink = stage.querySelector('[data-river-boat-farmer]');
    const hay = stage.querySelector('.river-hay');
    return {
      standaloneFarmers: stage.querySelectorAll('[data-river-character="FARMER"]').length,
      compositeFarmer: boatElement.querySelector('[data-river-boat-farmer]') !== null,
      boatImageSize: [boatImage.naturalWidth, boatImage.naturalHeight],
      hullAlignedWithBoat: (
        Math.abs(hull.left - boat.left) <= 0.5
        && Math.abs(hull.top - boat.top) <= 0.5
        && Math.abs(hull.width - boat.width) <= 0.5
        && Math.abs(hull.height - boat.height) <= 0.5
      ),
      hullClipPath: getComputedStyle(hullElement).clipPath,
      blinkingCharacters: stage.querySelectorAll('.river-blink-layer').length,
      hayHasBlinkLayer: stage.querySelector('.river-hay .river-blink-layer') !== null,
      blinkAnimation: getComputedStyle(farmerBlink).animationName,
      blinkPlayState: getComputedStyle(farmerBlink).animationPlayState,
      hayPosition: {
        left: hay.style.left,
        top: hay.style.top
      }
    };
  })()`);
  assert.equal(presentation.standaloneFarmers, 0);
  assert.equal(presentation.compositeFarmer, true);
  assert.ok(presentation.boatImageSize[0] >= 1500);
  assert.ok(presentation.boatImageSize[0] / presentation.boatImageSize[1] > 1.6);
  assert.ok(presentation.boatImageSize[0] / presentation.boatImageSize[1] < 1.9);
  assert.equal(presentation.hullAlignedWithBoat, true);
  assert.notEqual(presentation.hullClipPath, "none");
  assert.equal(presentation.blinkingCharacters, 3);
  assert.equal(presentation.hayHasBlinkLayer, false);
  assert.equal(presentation.blinkAnimation, "river-character-blink");
  assert.equal(presentation.blinkPlayState, "running");
  assert.deepEqual(presentation.hayPosition, { left: "29%", top: "54%" });
  await waitFor(
    "Number(getComputedStyle(document.querySelector('[data-river-boat-farmer]')).opacity) > 0.5",
    "the embedded farmer's closed-eye sprite to appear before the program runs",
    10000
  );
  await captureScreenshot("homepage-challenge-farmer-blink");
  await waitFor(
    "(() => {"
      + " const layers = [...document.querySelectorAll('.river-blink-layer')];"
      + " for (const layer of layers) {"
      + "   if (Number(getComputedStyle(layer).opacity) > 0.5) layer.dataset.riverBlinkObserved = 'true';"
      + " }"
      + " return layers.every((layer) => layer.dataset.riverBlinkObserved === 'true');"
      + "})()",
    "the farmer, sheep, and wolf closed-eye sprites to each appear",
    10000
  );
  await waitFor(
    "window.__riverAudioStarts.some((start) => Math.abs(start.volume - 0.16) < 0.001)",
    "the sheep idle sound to play while the opened challenge is idle",
    10000
  );
  await waitFor(
    "document.querySelector('.river-sheep').classList.contains('is-bellowing')"
      + " && Number(getComputedStyle(document.querySelector('.river-sheep-mouth')).opacity) > 0.9",
    "the sheep mouth to open with its idle bleat"
  );
  assert.ok(
    await evaluate("Number(getComputedStyle(document.querySelector('.river-sheep-mouth')).opacity)")
      > 0.9,
    "The sheep's open mouth must be visible while it bellows"
  );
  const mouthPosition = await evaluate(`(() => {
    const sheep = document.querySelector('.river-sheep').getBoundingClientRect();
    const mouth = document.querySelector('.river-sheep-mouth').getBoundingClientRect();
    return {
      x: (mouth.left + mouth.width / 2 - sheep.left) / sheep.width,
      y: (mouth.top + mouth.height / 2 - sheep.top) / sheep.height
    };
  })()`);
  assert.ok(Math.abs(mouthPosition.x - 0.56) <= 0.01);
  assert.ok(Math.abs(mouthPosition.y - 0.52) <= 0.01);
  await captureScreenshot("homepage-challenge-sheep-bellow");
  await waitFor(
    "!document.querySelector('.river-sheep').classList.contains('is-bellowing')",
    "the sheep mouth to close after its idle bleat",
    4000
  );
  const invalidSource = "get the hay in the boat\n\"unterminated";
  await evaluate(`(() => {
    const source = editorFor('[data-river-editor-shell]');
    source.executeEdits('test',[{range:source.getModel().getFullModelRange(),text:${JSON.stringify(invalidSource)}}]);
  })()`);
  await waitFor("editorMarkers('[data-river-editor-shell]').some(marker=>marker.startLineNumber===2)",'syntax errors refer to editable line two');
  assert.match(await evaluate("editorMarkers('[data-river-editor-shell]').map(marker=>marker.message).join(' ' )"),/unmatched string character/i);
  await captureScreenshot("homepage-challenge-syntax-error");
  await editCode('[data-river-editor-shell]',starterSource);
  await waitFor("editorMarkers('[data-river-editor-shell]').length===0",'valid source clears diagnostics');
  const joinedCommandSource = "get the sheep in the boat and get the hay in the boat";
  await evaluate(`(() => {
    const source = editorFor('[data-river-editor-shell]');
    source.executeEdits('test',[{range:source.getModel().getFullModelRange(),text:${JSON.stringify(joinedCommandSource)}}]);
  })()`);
  await evaluate("document.querySelector('[data-river-run]').click()");
  await waitFor(
    "document.querySelector('[data-river-game]').dataset.playbackState === 'failure'",
    "the second command in one DynLex expression to fail at runtime"
  );
  const joinedCommandFailure = await evaluate(`(() => {
    const marker = editorFeedback('[data-river-editor-shell]','error');
    return {
      diagnostics: document.querySelector('[data-river-diagnostics]').textContent,
      markerText: marker?.text,
      markerState: marker?.state,
      line: editorFeedback('[data-river-editor-shell]','error')?.line
    };
  })()`);
  assert.equal(joinedCommandFailure.diagnostics, "the boat is already carrying something");
  assert.equal(joinedCommandFailure.markerText, "get the hay in the boat");
  assert.equal(joinedCommandFailure.markerState, "error");
  assert.equal(joinedCommandFailure.line, "1");
  await evaluate("document.querySelector('[data-river-reset]').click()");
  await evaluate(`(() => {
    const source = editorFor('[data-river-editor-shell]');
    source.executeEdits('test',[{range:source.getModel().getFullModelRange(),text:${JSON.stringify(starterSource)}}]);
  })()`);
  await waitFor(
    "document.querySelector('[data-river-editor-shell]').dataset.languageReady === 'true'",
    "the starter source to recover after the joined-command runtime error"
  );
  await waitFor(
    "document.querySelector('[data-river-challenge]').dataset.musicState === 'audible'"
      + " && document.querySelector('[data-river-challenge]').dataset.ambienceState === 'audible'",
    "the challenge music and ambience to become audible while the opened challenge is visible"
  );
  await evaluate("document.querySelector('[data-river-mute]').click()");
  assert.equal(
    await evaluate("document.querySelector('[data-river-mute]').getAttribute('aria-pressed')"),
    "true"
  );
  assert.equal(
    await evaluate("document.querySelector('[data-river-challenge]').dataset.musicState"),
    "dampened"
  );
  assert.equal(
    await evaluate("document.querySelector('[data-river-challenge]').dataset.soundState"),
    "silent"
  );
  assert.equal(
    await evaluate("document.querySelector('[data-river-challenge]').dataset.ambienceState"),
    "dampened"
  );
  assert.match(
    await evaluate("document.querySelector('[data-river-mute]').textContent"),
    /AUDIO OFF/
  );
  await evaluate("document.querySelector('[data-river-mute]').click()");
  await waitFor(
    "document.querySelector('[data-river-challenge]').dataset.musicState === 'audible'"
      + " && document.querySelector('[data-river-challenge]').dataset.ambienceState === 'audible'",
    "the challenge music and ambience to return after unmuting"
  );
  await evaluate("document.querySelector('[data-river-speed]').click()");
  await evaluate("document.querySelector('[data-river-speed]').click()");
  assert.equal(await evaluate("document.querySelector('[data-river-speed]').textContent"), "4× SPEED");
  await evaluate("document.querySelector('[data-river-run]').click()");
  await waitFor(
    "document.querySelector('[data-river-game]').dataset.playbackState === 'running'",
    "the failing starter plan to begin animating"
  );
  await waitFor(
    "window.__riverAudioStarts.some((start) => Math.abs(start.volume - 0.38) < 0.001)",
    "loading the hay to play the boat sound"
  );
  await waitFor(
    "window.__riverAudioStarts.some((start) => Math.abs(start.volume - 0.34) < 0.001)",
    "crossing the river to play the rowing sound"
  );
  await evaluate("document.querySelector('[data-river-playback-toggle]').click()");
  await waitFor(
    "document.querySelector('[data-river-game]').dataset.playbackState === 'paused'",
    "the river animation to pause"
  );
  assert.equal(
    await evaluate("document.querySelector('[data-river-stage]').style.getPropertyValue('--river-animation-play-state')"),
    "paused"
  );
  await evaluate("document.querySelector('[data-river-playback-toggle]').click()");
  await waitFor(
    "document.querySelector('[data-river-game]').dataset.playbackState === 'running'",
    "the river animation to resume"
  );
  await evaluate("document.querySelector('#language').scrollIntoView({ block: 'start' })");
  await waitFor(
    "document.querySelector('[data-river-challenge]').dataset.musicState === 'dampened'"
      + " && document.querySelector('[data-river-challenge]').dataset.soundState === 'silent'"
      + " && document.querySelector('[data-river-challenge]').dataset.ambienceState === 'dampened'",
    "the challenge audio to dampen when the challenge leaves the viewport"
  );
  await new Promise((resolve) => setTimeout(resolve, 2500));
  await evaluate("document.querySelector('#challenges').scrollIntoView({ block: 'start' })");
  await waitFor(
    "document.querySelector('[data-river-world-state]').textContent.includes('paused')",
    "the failed animation to finish and reset while off screen"
  );
  const failure = await evaluate(`(() => ({
    diagnostics: document.querySelector('[data-river-diagnostics]').textContent,
    failedLine: editorFeedback('[data-river-editor-shell]','error')?.line,
    boatSide: document.querySelector('[data-river-stage]').dataset.boatSide,
    playbackState: document.querySelector('[data-river-game]').dataset.playbackState
  }))()`);
  assert.equal(failure.diagnostics, "the wolf ate the sheep");
  assert.equal(failure.failedLine, "3");
  assert.equal(failure.boatSide, "HOME");
  assert.equal(failure.playbackState, "failure");
  const tongueAnimations = await evaluate("window.__riverTongueAnimations");
  assert.equal(tongueAnimations.length, 1, "The wolf must lick exactly once after the fight");
  assert.equal(tongueAnimations[0].iterations, 1);
  assert.equal(tongueAnimations[0].duration, 850);
  assert.equal(tongueAnimations[0].fighting, false, "The lick must start after the fight ends");
  assert.ok(
    tongueAnimations[0].firstLeft > tongueAnimations[0].lastLeft,
    "The tongue must sweep from the right side of the mouth to the left"
  );
  assert.equal(
    await evaluate("document.querySelector('.river-wolf').classList.contains('is-licking')"),
    false,
    "The wolf must retract its tongue after the one-shot lick"
  );
  await new Promise((resolve) => setTimeout(resolve, 700));
  await captureScreenshot("homepage-challenge-failure");

  const loopSource = `set passenger to the sheep
loop 5 times:
    get passenger in the boat
    row to the other side
    get passenger out of the boat
    row to the other side`;
  await evaluate(`(() => {
    const source = editorFor('[data-river-editor-shell]');
    source.executeEdits('test',[{range:source.getModel().getFullModelRange(),text:${JSON.stringify(loopSource)}}]);
  })()`);
  await evaluate("document.querySelector('[data-river-run]').click()");
  await waitFor(
    "document.querySelector('[data-river-game]').dataset.playbackState === 'failure'",
    "the repeated DynLex loop to reach its runtime failure"
  );
  const loopFailure = await evaluate(`(() => ({
    diagnostics: document.querySelector('[data-river-diagnostics]').textContent,
    failedLine: editorFeedback('[data-river-editor-shell]','error')?.line,
    failedCall: editorFeedback('[data-river-editor-shell]','error')?.text
  }))()`);
  assert.equal(loopFailure.diagnostics, "there is no sheep to pick up");
  assert.equal(loopFailure.failedLine, "3");
  assert.equal(loopFailure.failedCall, "get passenger in the boat");

  const headingSource = `get the sheep in the boat
row to the other side
row back`;
  await evaluate("document.querySelector('[data-river-speed]').click()");
  assert.equal(await evaluate("document.querySelector('[data-river-speed]').textContent"), "1× SPEED");
  await evaluate(`(() => {
    const source = editorFor('[data-river-editor-shell]');
    source.executeEdits('test',[{range:source.getModel().getFullModelRange(),text:${JSON.stringify(headingSource)}}]);
  })()`);
  await evaluate("document.querySelector('[data-river-run]').click()");
  await waitFor(
    "document.querySelector('[data-river-game]').dataset.playbackState === 'running'"
      + " && document.querySelector('[data-river-stage]').dataset.boatHeading === 'HOME'"
      + " && document.querySelector('[data-river-stage]').dataset.boatSide === 'HOME'",
    "the homebound boat to face its direction of travel while crossing"
  );
  await evaluate("document.querySelector('[data-river-playback-toggle]').click()");
  await waitFor(
    "document.querySelector('[data-river-game]').dataset.playbackState === 'paused'",
    "the homebound crossing to pause for visual inspection"
  );
  const homebound = await evaluate(`(() => {
    const stage = document.querySelector('[data-river-stage]');
    const boat = stage.querySelector('[data-river-boat]');
    const hull = stage.querySelector('[data-river-boat-hull]');
    const sheep = stage.querySelector('[data-river-character="SHEEP"]');
    const wolf = stage.querySelector('[data-river-character="WOLF"]');
    const boatRect = boat.getBoundingClientRect();
    const sheepRect = sheep.getBoundingClientRect();
    const horizontalScale = (element) => new DOMMatrix(getComputedStyle(element).transform).a;
    return {
      heading: stage.dataset.boatHeading,
      boatScale: horizontalScale(boat),
      hullScale: horizontalScale(hull),
      wolfScale: horizontalScale(wolf),
      boatX: boatRect.left + boatRect.width / 2,
      sheepX: sheepRect.left + sheepRect.width / 2,
      compositeFarmer: boat.querySelector('[data-river-boat-farmer]') !== null,
      sheepOwnedByBoat: sheep.parentElement === boat,
      sheepClearOfFarmer: sheepRect.right <= boatRect.left + boatRect.width * 0.5,
      sheepInsideBoat: (
        sheepRect.left >= boatRect.left
        && sheepRect.right <= boatRect.right
        && sheepRect.top >= boatRect.top
        && sheepRect.bottom <= boatRect.bottom
      )
    };
  })()`);
  assert.equal(homebound.heading, "HOME");
  assert.ok(homebound.boatScale < 0);
  assert.ok(homebound.hullScale < 0);
  assert.ok(homebound.wolfScale > 0);
  assert.ok(homebound.sheepX < homebound.boatX);
  assert.equal(homebound.compositeFarmer, true);
  assert.equal(homebound.sheepOwnedByBoat, true);
  assert.equal(homebound.sheepClearOfFarmer, true);
  assert.equal(homebound.sheepInsideBoat, true);
  await captureScreenshot("homepage-challenge-homebound");
  await evaluate("document.querySelector('[data-river-playback-toggle]').click()");
  await waitFor(
    "document.querySelector('[data-river-game]').dataset.playbackState === 'paused'"
      + " && document.querySelector('[data-river-world-state]').textContent"
      + " === 'waiting for the next instruction'",
    "the homebound plan to finish after visual inspection"
  );

  const passengerFitSource = `get the sheep in the boat
get the sheep out of the boat
get the wolf in the boat
get the wolf out of the boat
get the hay in the boat`;
  const passengerOpaqueBounds = {
    SHEEP: { left: 167 / 750, top: 96 / 750, right: 667 / 750, bottom: 608 / 750 },
    WOLF: { left: 109 / 750, top: 128 / 750, right: 531 / 750, bottom: 705 / 750 },
    HAY: { left: 156 / 750, top: 345 / 750, right: 717 / 750, bottom: 597 / 750 }
  };
  const passengerBankWidths = await evaluate(`(() => {
    const source = editorFor('[data-river-editor-shell]');
    source.executeEdits('test',[{range:source.getModel().getFullModelRange(),text:${JSON.stringify(passengerFitSource)}}]);
    return Object.fromEntries(
      [...document.querySelectorAll('[data-river-character]')]
        .map((actor) => [actor.dataset.riverCharacter, actor.getBoundingClientRect().width])
    );
  })()`);
  await evaluate("document.querySelector('[data-river-run]').click()");
  const passengerFeet = {};
  for (const [line, subject] of [[1, "SHEEP"], [3, "WOLF"], [5, "HAY"]]) {
    await waitFor(
      `editorFeedback('[data-river-editor-shell]','active')?.line === '${line}'`
        + ` && document.querySelector('[data-river-character="${subject}"]').parentElement`
        + " === document.querySelector('[data-river-boat]')"
        + ` && document.querySelector('[data-river-character="${subject}"]').getAnimations().length > 0`,
      `${subject.toLowerCase()} to start boarding for its fit check`
    );
    await evaluate("document.querySelector('[data-river-playback-toggle]').click()");
    const bounds = await aboardSpriteBounds(evaluate, subject, passengerOpaqueBounds[subject]);
    const scale = bounds.actorWidth / passengerBankWidths[subject];
    assert.ok(
      Math.abs(scale - 0.8) <= 0.01,
      `${subject.toLowerCase()} passenger scale must be 80%, got ${scale}`
    );
    assert.ok(bounds.opaque.left >= bounds.boat.left);
    assert.ok(bounds.opaque.right <= bounds.boat.right);
    const floor = bounds.boat.top + bounds.boat.height * 0.625;
    passengerFeet[subject] = bounds.opaque.bottom;
    assert.ok(
      Math.abs(bounds.opaque.bottom - floor) <= 1,
      `${subject.toLowerCase()} must stand on its assigned boat floor`
    );
    await evaluate("document.querySelector('[data-river-playback-toggle]').click()");
  }
  assert.ok(
    Math.abs(passengerFeet.SHEEP - passengerFeet.WOLF) <= 0.5,
    "The sheep and wolf feet must use the same boat floor"
  );
  await waitFor(
    "document.querySelector('[data-river-game]').dataset.playbackState === 'paused'"
      + " && document.querySelector('[data-river-world-state]').textContent"
      + " === 'waiting for the next instruction'",
    "the passenger fit plan to finish"
  );

  const secondSheepCrossingSource = `get the sheep in the boat
row to the other side
get the sheep out of the boat
row to the other side
get the wolf in the boat
row to the other side
get the wolf out of the boat
get the sheep in the boat
row to the other side
get the sheep out of the boat
get the hay in the boat`;
  const hayBankCenter = await evaluate(`(() => {
    const source = editorFor('[data-river-editor-shell]');
    source.executeEdits('test',[{range:source.getModel().getFullModelRange(),text:${JSON.stringify(secondSheepCrossingSource)}}]);
    const hay = document.querySelector('[data-river-character="HAY"]').getBoundingClientRect();
    return {
      x: hay.left + hay.width / 2,
      y: hay.top + hay.height / 2
    };
  })()`);
  await evaluate("document.querySelector('[data-river-run]').click()");
  await waitFor(
    "editorFeedback('[data-river-editor-shell]','active')?.line === '11'"
      + " && document.querySelector('[data-river-character=\"HAY\"]').parentElement"
      + " === document.querySelector('[data-river-boat]')"
      + " && document.querySelector('[data-river-character=\"HAY\"]').getAnimations().length > 0",
    "the hay to start boarding after the sheep's second crossing"
  );
  await evaluate("document.querySelector('[data-river-playback-toggle]').click()");
  const hayBoardingOrigin = await evaluate(`(() => {
    const hay = document.querySelector('[data-river-character="HAY"]');
    const animation = hay.getAnimations()[0];
    const currentTime = animation.currentTime;
    animation.currentTime = 0;
    const rect = hay.getBoundingClientRect();
    const origin = { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
    animation.currentTime = currentTime;
    return origin;
  })()`);
  assert.ok(
    Math.hypot(
      hayBoardingOrigin.x - hayBankCenter.x,
      hayBoardingOrigin.y - hayBankCenter.y
    ) <= 1,
    "The hay boarding animation must start at the hay's current bank position: "
      + `expected ${JSON.stringify(hayBankCenter)}, got ${JSON.stringify(hayBoardingOrigin)}`
  );
  await evaluate("document.querySelector('[data-river-playback-toggle]').click()");
  await waitFor(
    "document.querySelector('[data-river-game]').dataset.playbackState === 'paused'"
      + " && document.querySelector('[data-river-world-state]').textContent"
      + " === 'waiting for the next instruction'",
    "the second-sheep-crossing plan to finish"
  );

  const solvedSource = `# The official names are: sheep, wolf, and hay.
get the sheep in the boat
row to the other side
get the sheep out of the boat
row to the other side
get the wolf in the boat
row to the other side
get the wolf out of the boat
get the sheep in the boat
row to the other side
get the sheep out of the boat
get the hay in the boat
row to the other side
get the hay out of the boat
row to the other side
get the sheep in the boat
row to the other side
get the sheep out of the boat`;
  await evaluate(`(() => {
    const source = editorFor('[data-river-editor-shell]');
    source.executeEdits('test',[{range:source.getModel().getFullModelRange(),text:${JSON.stringify(solvedSource)}}]);
  })()`);
  assert.equal(await evaluate("document.querySelector('[data-river-diagnostics]').hidden"), true);
  assert.equal(await evaluate("editorFeedback('[data-river-editor-shell]','active') === null && editorFeedback('[data-river-editor-shell]','error') === null"), true);
  await evaluate("document.querySelector('[data-river-run]').click()");
  await waitFor(
    "document.querySelector('[data-river-game]').dataset.playbackState === 'success'",
    "the correct river plan to finish"
  );
  const sounds = await evaluate("window.__riverAudioStarts");
  const anxiousSound = sounds.find((start) => Math.abs(start.volume - 0.52) < 0.001);
  assert.ok(anxiousSound, "The wolf attack must play the anxious sheep sound");
  assert.ok(
    anxiousSound.playbackRate > 4,
    `The anxious sheep sound must be pitched above 4× playback, got ${anxiousSound.playbackRate}`
  );
  assert.ok(
    sounds.some((start) => Math.abs(start.volume - 0.46) < 0.001),
    "Solving the challenge must play the win sound"
  );
  const success = await evaluate(`(() => {
    const stage = document.querySelector('[data-river-stage]');
    const boat = stage.querySelector('[data-river-boat]').getBoundingClientRect();
    const bubbleElement = stage.querySelector('[data-river-speech]');
    const bubble = bubbleElement.getBoundingClientRect();
    const tail = getComputedStyle(bubbleElement, '::after');
    const farmerHeadX = boat.left + boat.width * 0.36;
    const tailCenterX = bubble.left + parseFloat(tail.left) + parseFloat(tail.width) / 2;
    return {
      complete: stage.dataset.sceneComplete,
      speech: document.querySelector('[data-river-speech-text]').textContent,
      speechSide: bubbleElement.dataset.side,
      status: document.querySelector('[data-river-status]').textContent,
      speechTailHorizontalOffset: Math.abs(tailCenterX - farmerHeadX),
      speechVerticalGap: boat.top - bubble.bottom,
      boatHeight: boat.height,
      speechNearFarmerVertically: (
        bubble.bottom >= boat.top - boat.height * 0.75
        && bubble.bottom <= boat.top + boat.height * 0.12
      )
    };
  })()`);
  assert.equal(success.complete, "true");
  assert.equal(success.speech, "We made it! What a brilliant plan!");
  assert.equal(success.speechSide, "FAR");
  assert.equal(success.status, "SOLVED");
  assert.ok(
    success.speechTailHorizontalOffset <= 25,
    `Victory speech tail is ${success.speechTailHorizontalOffset}px from the farmer`
  );
  assert.equal(
    success.speechNearFarmerVertically,
    true,
    `Victory speech vertical gap is ${success.speechVerticalGap}px for a ${success.boatHeight}px boat`
  );
  assert.equal(
    requestedUrls.filter((url) => new URL(url).pathname === "/compiler/compiler-worker.js").length,
    1,
    "The river challenge must reuse the homepage compiler worker"
  );
  await waitFor(
    "document.querySelector('[data-river-challenge]').dataset.challengeState === 'farm'",
    "the solved river challenge to advance to the farm"
  );
  assert.equal(await evaluate("document.querySelector('[data-river-game]') === null"), true);
  await captureScreenshot("homepage-challenges");
}
