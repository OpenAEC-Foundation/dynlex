import assert from "node:assert/strict";
import {
  captureScreenshot,
  closeBrowserSession,
  command,
  evaluate,
  navigate,
  runtimeExceptions,
  waitFor
} from "./browser_test_driver.mjs";

await command("Emulation.setDeviceMetricsOverride", {
  width: 1440,
  height: 1400,
  deviceScaleFactor: 1,
  mobile: false
});
await navigate("/river-character-sheet.html");
await waitFor(
  "document.documentElement.dataset.riverSheetReady === 'true'",
  "the river character sheet"
);
await waitFor("document.fonts.status === 'loaded'", "the character sheet fonts");

const summary = await evaluate(`(() => {
  const sheet = document.querySelector('[data-river-character-sheet]');
  const wolfLick = document.querySelector('[data-character="WOLF"][data-state="licking"]');
  const wolf = wolfLick.querySelector('.river-wolf');
  const tongue = wolfLick.querySelector('.river-wolf-tongue');
  const tongueImage = tongue.querySelector('svg');
  const tongueAnimations = tongue.getAnimations({ subtree: true });
  const positionAnimation = tongueAnimations.find((animation) => animation.effect.target === tongue);
  const extensionAnimation = tongueAnimations.find((animation) => animation.effect.target === tongueImage);
  const positionFrames = positionAnimation.effect.getKeyframes();
  const extensionFrames = extensionAnimation.effect.getKeyframes();
  const wolfWidth = Number.parseFloat(getComputedStyle(wolf).width);
  const sheepBellow = document.querySelector('[data-character="SHEEP"][data-state="bellowing"]');
  const blink = document.querySelector('[data-character="WOLF"][data-state="blink"] .river-wolf-blink');
  return {
    groups: document.querySelectorAll('[data-character-group]').length,
    cards: document.querySelectorAll('.river-sheet-card').length,
    isolatedEyelids: document.querySelectorAll('.river-sheet-isolated-eyelid').length,
    mouthLineHighlights: document.querySelectorAll('.river-wolf-mouth-line').length,
    characters: [...document.querySelectorAll('[data-character-group]')]
      .map((group) => group.dataset.characterGroup),
    animations: sheet.getAnimations({ subtree: true }).length,
    wolfLickAnimations: tongueAnimations.length,
    wolfLickCssAnimation: getComputedStyle(tongue).animationName,
    lickDirection: {
      firstLeft: Number.parseFloat(positionFrames.at(0).left),
      lastLeft: Number.parseFloat(positionFrames.at(-1).left)
    },
    lickExtension: {
      firstTransform: extensionFrames.at(0).transform,
      firstOpacity: Number(extensionFrames.at(0).opacity),
      lastTransform: extensionFrames.at(-1).transform,
      lastOpacity: Number(extensionFrames.at(-1).opacity)
    },
    tonguePathCount: wolfLick.querySelectorAll('.river-wolf-tongue-shape').length,
    tonguePath: wolfLick.querySelector('.river-wolf-tongue-shape').getAttribute('d'),
    tongueViewBox: wolfLick.querySelector('.river-wolf-tongue svg').getAttribute('viewBox'),
    tongueSize: {
      width: Number.parseFloat(getComputedStyle(tongue).width) / wolfWidth,
      height: Number.parseFloat(getComputedStyle(tongue).height) / wolfWidth
    },
    sheepMouthOpacity: Number(getComputedStyle(sheepBellow.querySelector('.river-sheep-mouth')).opacity),
    forcedBlinkOpacity: Number(getComputedStyle(blink).opacity)
  };
})()`);

assert.equal(summary.groups, 4);
assert.equal(summary.cards, 17);
assert.equal(summary.isolatedEyelids, 2);
assert.equal(summary.mouthLineHighlights, 1);
assert.deepEqual(summary.characters, ["BOAT", "SHEEP", "WOLF", "HAY"]);
assert.ok(summary.animations >= 8, "Every production loop must be active on the sheet");
assert.equal(summary.wolfLickAnimations, 2);
assert.equal(summary.wolfLickCssAnimation, "none");
assert.ok(summary.lickDirection.firstLeft > summary.lickDirection.lastLeft);
assert.deepEqual(summary.lickExtension, {
  firstTransform: "scaleX(0)",
  firstOpacity: 0,
  lastTransform: "scaleX(0)",
  lastOpacity: 0
});
assert.equal(summary.tonguePathCount, 1);
assert.equal(summary.tonguePath, "M0 0 L30 2 A9 18 0 0 1 30 38 L0 40 Z");
assert.equal(summary.tongueViewBox, "0 0 39 40");
assert.ok(Math.abs(summary.tongueSize.width - 0.052) < 0.001, "The tongue length must be halved");
assert.ok(Math.abs(summary.tongueSize.height - 0.05333) < 0.001, "The tongue thickness must stay unchanged");
assert.equal(summary.sheepMouthOpacity, 1);
assert.equal(summary.forcedBlinkOpacity, 1);

await evaluate(`(() => {
  const boxes = document.querySelector('[data-river-sheet-boxes]');
  boxes.checked = true;
  boxes.dispatchEvent(new Event('change', { bubbles: true }));
  const scrubber = document.querySelector('[data-river-sheet-scrubber]');
  scrubber.value = '50';
  scrubber.dispatchEvent(new Event('input', { bubbles: true }));
})()`);
assert.equal(
  await evaluate("document.querySelector('[data-river-character-sheet]').dataset.overlayBoxes"),
  "true"
);
assert.equal(
  await evaluate("document.querySelector('[data-river-sheet-playback]').textContent"),
  "PLAY LOOPS"
);
assert.equal(
  await evaluate(
    "document.querySelector('[data-river-character-sheet]').getAnimations({ subtree: true })"
      + ".every((animation) => animation.playState === 'paused')"
  ),
  true
);
const overlayGeometry = await evaluate(`(() => {
  function screenPoint(element, point) {
    const matrix = element.getScreenCTM();
    if (matrix === null) throw new Error('River sheet SVG has no screen transform');
    return new DOMPoint(point.x, point.y).matrixTransform(matrix);
  }
  function mouthIntersectsTongue(mouthLine, tongueShape) {
    const inverseTongue = tongueShape.getScreenCTM().inverse();
    for (const path of mouthLine.querySelectorAll('path')) {
      const length = path.getTotalLength();
      for (let step = 0; step <= 300; ++step) {
        const at = length * step / 300;
        const point = path.getPointAtLength(at);
        const before = path.getPointAtLength(Math.max(0, at - 1));
        const after = path.getPointAtLength(Math.min(length, at + 1));
        const magnitude = Math.hypot(after.x - before.x, after.y - before.y);
        const normal = { x: -(after.y - before.y) / magnitude, y: (after.x - before.x) / magnitude };
        for (const offset of [-4, 0, 4]) {
          const mouthPoint = screenPoint(path, {
            x: point.x + normal.x * offset,
            y: point.y + normal.y * offset
          });
          if (tongueShape.isPointInFill(mouthPoint.matrixTransform(inverseTongue))) return true;
        }
      }
    }
    return false;
  }
  function tongueRootGeometry(mainMouthLine, tongueShape) {
    const root = screenPoint(tongueShape, { x: 0, y: 20 });
    const rootStart = screenPoint(tongueShape, { x: 0, y: 0 });
    const rootEnd = screenPoint(tongueShape, { x: 0, y: 40 });
    const length = mainMouthLine.getTotalLength();
    let nearestAt = 0;
    let nearestDistance = Number.POSITIVE_INFINITY;
    for (let step = 0; step <= 600; ++step) {
      const at = length * step / 600;
      const point = screenPoint(mainMouthLine, mainMouthLine.getPointAtLength(at));
      const distance = Math.hypot(point.x - root.x, point.y - root.y);
      if (distance < nearestDistance) {
        nearestAt = at;
        nearestDistance = distance;
      }
    }
    const before = screenPoint(mainMouthLine, mainMouthLine.getPointAtLength(Math.max(0, nearestAt - 1)));
    const after = screenPoint(mainMouthLine, mainMouthLine.getPointAtLength(Math.min(length, nearestAt + 1)));
    const rootLength = Math.hypot(rootEnd.x - rootStart.x, rootEnd.y - rootStart.y);
    const tangentLength = Math.hypot(after.x - before.x, after.y - before.y);
    return {
      alignment: ((rootEnd.x - rootStart.x) * (after.x - before.x)
        + (rootEnd.y - rootStart.y) * (after.y - before.y)) / (rootLength * tangentLength),
      distance: nearestDistance
    };
  }
  const wolf = document.querySelector('[data-character="WOLF"][data-state="licking"] .river-wolf');
  const sheepBlink = document.querySelector('.river-sheep-blink');
  const wolfBlink = document.querySelector('.river-wolf-blink');
  const tongue = wolf.querySelector('.river-wolf-tongue');
  const tongueImage = tongue.querySelector('svg');
  const tongueShape = tongue.querySelector('.river-wolf-tongue-shape');
  const mouthLine = wolf.querySelector('.river-wolf-mouth-line');
  const mainMouthLine = mouthLine.querySelector('path');
  const lickAnimations = tongue.getAnimations({ subtree: true });
  const tongueAnimation = lickAnimations.find((animation) => animation.effect.target === tongue);
  const extensionAnimation = lickAnimations.find((animation) => animation.effect.target === tongueImage);
  const duration = tongueAnimation.effect.getComputedTiming().duration;
  function tongueFrame(percent) {
    tongueAnimation.currentTime = duration * percent / 100;
    extensionAnimation.currentTime = duration * percent / 100;
    const root = tongueRootGeometry(mainMouthLine, tongueShape);
    return {
      rootAlignment: root.alignment,
      rootDistance: root.distance,
      rootX: screenPoint(tongueShape, { x: 0, y: 20 }).x,
      intersectsMouth: mouthIntersectsTongue(mouthLine, tongueShape),
      opacity: Number(getComputedStyle(tongueImage).opacity)
    };
  }
  const emerging = tongueFrame(1);
  const right = tongueFrame(18);
  const middle = tongueFrame(50);
  const left = tongueFrame(82);
  const retracting = tongueFrame(99);
  const intersectingTimelineFrames = [];
  for (let percent = 1; percent < 100; ++percent) {
    const frame = tongueFrame(percent);
    if (frame.intersectsMouth) intersectingTimelineFrames.push(percent);
  }
  return {
    sheepMask: getComputedStyle(sheepBlink).maskImage,
    wolfMask: getComputedStyle(wolfBlink).maskImage,
    sheepLeftEyelid: getComputedStyle(sheepBlink, '::before').backgroundImage,
    sheepRightEyelid: getComputedStyle(sheepBlink, '::after').backgroundImage,
    wolfLeftEyelid: getComputedStyle(wolfBlink, '::before').backgroundImage,
    wolfRightEyelid: getComputedStyle(wolfBlink, '::after').backgroundImage,
    sheepLeftMask: getComputedStyle(sheepBlink, '::before').maskImage,
    sheepRightMask: getComputedStyle(sheepBlink, '::after').maskImage,
    wolfLeftMask: getComputedStyle(wolfBlink, '::before').maskImage,
    wolfRightMask: getComputedStyle(wolfBlink, '::after').maskImage,
    mouthLine: {
      display: getComputedStyle(mouthLine).display,
      length: mouthLine.querySelector('path').getTotalLength(),
      viewBox: mouthLine.getAttribute('viewBox')
    },
    emerging,
    right,
    middle,
    left,
    retracting,
    intersectingTimelineFrames
  };
})()`);
assert.equal(overlayGeometry.sheepMask, "none");
assert.equal(overlayGeometry.wolfMask, "none");
assert.match(overlayGeometry.sheepLeftEyelid, /sheep-blink\.webp/);
assert.match(overlayGeometry.sheepRightEyelid, /sheep-blink\.webp/);
assert.match(overlayGeometry.wolfLeftEyelid, /wolf-blink\.webp/);
assert.match(overlayGeometry.wolfRightEyelid, /wolf-blink\.webp/);
for (const mask of [
  overlayGeometry.sheepLeftMask,
  overlayGeometry.sheepRightMask,
  overlayGeometry.wolfLeftMask,
  overlayGeometry.wolfRightMask
]) {
  assert.match(mask, /radial-gradient/);
}
assert.equal(overlayGeometry.mouthLine.display, "block");
assert.equal(overlayGeometry.mouthLine.viewBox, "0 0 750 750");
assert.ok(overlayGeometry.mouthLine.length > 200, "The highlighted mouth line is incomplete");
for (const [name, frame] of Object.entries({
  emerging: overlayGeometry.emerging,
  right: overlayGeometry.right,
  middle: overlayGeometry.middle,
  left: overlayGeometry.left,
  retracting: overlayGeometry.retracting
})) {
  assert.equal(frame.intersectsMouth, false, `${name}: the tongue intersects the painted mouth line`);
}
for (const frame of [
  overlayGeometry.emerging,
  overlayGeometry.right,
  overlayGeometry.middle,
  overlayGeometry.left,
  overlayGeometry.retracting
]) {
  assert.ok(
    Math.abs(frame.rootAlignment) > 0.97,
    `The tongue root edge misses the mouth slope (${frame.rootAlignment})`
  );
  assert.ok(frame.rootDistance < 2, `The tongue root is detached from the mouth (${frame.rootDistance}px)`);
}
assert.ok(overlayGeometry.emerging.opacity < overlayGeometry.right.opacity);
assert.equal(overlayGeometry.right.opacity, 1);
assert.equal(overlayGeometry.middle.opacity, 1);
assert.equal(overlayGeometry.left.opacity, 1);
assert.ok(overlayGeometry.retracting.opacity < overlayGeometry.left.opacity);
assert.ok(
  overlayGeometry.right.rootX > overlayGeometry.left.rootX,
  "The lick must sweep from the mouth's right side to its left side"
);
assert.deepEqual(overlayGeometry.intersectingTimelineFrames, []);

await evaluate(
  "document.querySelector('[data-character=\"WOLF\"][data-state=\"licking\"]')"
    + ".scrollIntoView({ block: 'center', behavior: 'instant' })"
);
await captureScreenshot("river-character-sheet");
assert.deepEqual(runtimeExceptions, [], "The river character sheet must not raise browser exceptions");
await closeBrowserSession();
console.log("River character animation sheet renders and scrubs in Chrome.");
