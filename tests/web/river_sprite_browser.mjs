export async function aboardSpriteBounds(evaluate, subject, opaqueBounds) {
  return evaluate(`(() => {
    const actor = document.querySelector('[data-river-character="${subject}"]');
    const boat = document.querySelector('[data-river-boat]');
    const animation = actor.getAnimations()[0];
    const currentTime = animation.currentTime;
    animation.currentTime = animation.effect.getTiming().duration;
    const actorRect = actor.getBoundingClientRect();
    const boatRect = boat.getBoundingClientRect();
    const mirrored = new DOMMatrix(getComputedStyle(boat).transform).a < 0;
    const opaque = ${JSON.stringify(opaqueBounds)};
    const leftRatio = mirrored ? 1 - opaque.right : opaque.left;
    const rightRatio = mirrored ? 1 - opaque.left : opaque.right;
    const result = {
      actorWidth: actorRect.width,
      boat: {
        left: boatRect.left,
        top: boatRect.top,
        right: boatRect.right,
        height: boatRect.height
      },
      opaque: {
        left: actorRect.left + actorRect.width * leftRatio,
        top: actorRect.top + actorRect.height * opaque.top,
        right: actorRect.left + actorRect.width * rightRatio,
        bottom: actorRect.top + actorRect.height * opaque.bottom
      }
    };
    animation.currentTime = currentTime;
    return result;
  })()`);
}
