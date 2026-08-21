import {
  applyRiverEvent,
  createInitialRiverScene,
  parseRiverTrace
} from "./river-challenge-model.js";
import {
  applyRiverSourceEdit,
  clearRiverLineStates,
  createRiverCompletions,
  prefixedRiverPosition,
  renderRiverCallRange,
  renderRiverCompilerDiagnostics,
  renderRiverDiagnosticRanges,
  renderRiverLspFeedback,
  renderRiverLspLineFeedback,
  renderRiverSourceLine,
  riverCommandCallRanges,
  riverSingleChangedLine,
  riverSourcePosition,
  RIVER_PROGRAM_PREFIX,
  RIVER_PROGRAM_PREFIX_LINES,
  RIVER_STARTER_SOURCE,
  setRiverLineState
} from "./river-challenge-editor.js";
import { createRiverChallengeAudio } from "./river-challenge-audio.js";
import {
  createWolfTongue,
  createWolfTongueLickSpecifications,
  WOLF_TONGUE_LICK_DURATION
} from "./river-character-art.js";

const LANDSCAPE_URL = new URL("./media/river-challenge/painted-river.webp", import.meta.url).href;
const BOAT_URL = new URL("./media/river-challenge/boat.webp", import.meta.url).href;
const BOAT_BLINK_URL = new URL("./media/river-challenge/boat-blink.webp", import.meta.url).href;
const SHEEP_URL = new URL("./media/river-challenge/sheep.webp", import.meta.url).href;
const SHEEP_BLINK_URL = new URL("./media/river-challenge/sheep-blink.webp", import.meta.url).href;
const WOLF_URL = new URL("./media/river-challenge/wolf.webp", import.meta.url).href;
const WOLF_BLINK_URL = new URL("./media/river-challenge/wolf-blink.webp", import.meta.url).href;
const HAY_URL = new URL("./media/river-challenge/hay.webp", import.meta.url).href;
const SPEEDS = [1, 2, 4];
const REDUCED_MOTION = window.matchMedia("(prefers-reduced-motion: reduce)");

function required(selector, scope = document) {
  const element = scope.querySelector(selector);
  if (!element) {
    throw new Error(`Missing required river challenge element: ${selector}`);
  }
  return element;
}

function loadImage(url) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    image.decoding = "async";
    image.addEventListener("load", () => resolve(image), { once: true });
    image.addEventListener("error", () => reject(new Error(`River challenge image failed: ${url}`)), {
      once: true
    });
    image.src = url;
  });
}

function riverMarkup() {
  return `
    <article class="river-game" data-river-game aria-label="Wolf, sheep, and hay coding challenge">
      <header class="river-game-header">
        <span>CHALLENGE_01</span>
        <strong>river-crossing.dl</strong>
        <i data-river-status role="status" aria-live="polite">READY</i>
      </header>
      <div class="river-game-body">
        <section class="river-editor-panel" aria-label="DynLex program">
          <div class="river-panel-label">
            <span>YOUR PLAN</span>
            <small>plain English / real DynLex</small>
          </div>
          <div class="river-editor-shell" data-river-editor-shell>
            <i class="river-source-line-state" data-river-source-line-state hidden></i>
            <pre class="river-source-highlight" data-river-source-highlight aria-hidden="true"><code data-river-source-code></code></pre>
            <pre class="river-source-playback" data-river-source-playback aria-hidden="true"><code data-river-call-code></code></pre>
            <pre class="river-source-diagnostics" data-river-source-diagnostics aria-hidden="true"><code data-river-diagnostic-code></code></pre>
            <textarea
              class="river-source"
              data-river-source
              aria-label="River crossing DynLex source"
              spellcheck="false"
              wrap="off"
              autocapitalize="off"
              autocomplete="off"
              aria-autocomplete="list"
              aria-controls="river-completions"
            ></textarea>
            <div
              class="river-completions"
              id="river-completions"
              data-river-completions
              role="listbox"
              hidden
            ></div>
          </div>
          <div class="river-diagnostics" data-river-diagnostics role="status" aria-live="polite" hidden></div>
          <div class="river-editor-actions">
            <span>⌘ / CTRL + ENTER</span>
            <button type="button" data-river-run>RUN PLAN <i aria-hidden="true">▶</i></button>
          </div>
        </section>
        <section class="river-world-panel" aria-label="Animated river crossing">
          <div class="river-panel-label river-world-label">
            <span>THE RIVER</span>
            <small data-river-world-state>waiting for a plan</small>
          </div>
          <div class="river-stage" data-river-stage>
            <img class="river-landscape" src="${LANDSCAPE_URL}" alt="">
            <div class="river-light" aria-hidden="true"></div>
            <span class="river-shore-label river-shore-home">HOME BANK</span>
            <span class="river-shore-label river-shore-far">FAR BANK</span>
            <div class="river-boat" data-river-boat role="img" aria-label="Boat with farmer">
              <img src="${BOAT_URL}" alt="">
              <i class="river-blink-layer river-boat-farmer-blink" data-river-boat-farmer aria-hidden="true"></i>
            </div>
            <div class="river-character river-sheep" data-river-character="SHEEP" role="img" aria-label="Sheep"><i class="river-blink-layer river-sheep-blink" aria-hidden="true"></i><i class="river-sheep-mouth" aria-hidden="true"></i></div>
            <div class="river-character river-wolf" data-river-character="WOLF" role="img" aria-label="Wolf"><i class="river-blink-layer river-wolf-blink" aria-hidden="true"></i></div>
            <div class="river-character river-hay" data-river-character="HAY" role="img" aria-label="Hay"></div>
            <div class="river-boat-hull" data-river-boat-hull aria-hidden="true">
              <img src="${BOAT_URL}" alt="">
            </div>
            <div class="river-dust" data-river-dust aria-hidden="true">
              <i></i><i></i><i></i><i></i><i></i>
            </div>
            <div class="river-speech" data-river-speech data-side="FAR" hidden>
              <span data-river-speech-text></span>
            </div>
          </div>
          <div class="river-playback">
            <button type="button" data-river-playback-toggle disabled aria-label="Play animation">
              <span aria-hidden="true">▶</span> PLAY
            </button>
            <button type="button" data-river-speed aria-label="Change animation speed">1× SPEED</button>
            <button type="button" data-river-reset>↺ RESET</button>
            <button type="button" data-river-mute aria-pressed="false">
              <span aria-hidden="true">♪</span> AUDIO ON
            </button>
          </div>
          <p class="river-program-message" data-river-message aria-live="polite">
            The farmer can take one passenger at a time.
          </p>
        </section>
      </div>
      <footer class="river-game-credit">
        <span>PAINTED FOR DYNLEX / ORIGINAL GAME ART</span>
        <a href="https://pixabay.com/music/happy-childrens-tunes-puzzle-amp-casual-game-music-460543/">
          MUSIC: “PUZZLE &amp; CASUAL GAME MUSIC” — SOUNOVAMUSIC / PIXABAY ↗
        </a>
      </footer>
    </article>
  `;
}

class PlaybackController {
  constructor(stage, onChange) {
    this.stage = stage;
    this.onChange = onChange;
    this.speed = 1;
    this.paused = true;
    this.activeAnimations = new Set();
    this.abortController = null;
  }

  begin() {
    this.stop();
    this.abortController = new AbortController();
    this.paused = false;
    this.sync();
    return this.abortController.signal;
  }

  stop() {
    if (this.abortController) {
      this.abortController.abort();
      this.abortController = null;
    }
    for (const animation of this.activeAnimations) {
      animation.cancel();
    }
    this.activeAnimations.clear();
    this.paused = true;
    this.sync();
  }

  toggle() {
    this.paused = !this.paused;
    for (const animation of this.activeAnimations) {
      if (this.paused) animation.pause();
      else animation.play();
    }
    this.sync();
  }

  cycleSpeed() {
    const index = SPEEDS.indexOf(this.speed);
    this.speed = SPEEDS[(index + 1) % SPEEDS.length];
    for (const animation of this.activeAnimations) {
      animation.updatePlaybackRate(this.speed);
    }
    this.sync();
  }

  sync() {
    this.stage.style.setProperty(
      "--river-animation-play-state",
      this.paused ? "paused" : "running"
    );
    this.onChange({ paused: this.paused, speed: this.speed });
  }

  async animate(specifications, duration, signal) {
    if (signal.aborted || REDUCED_MOTION.matches || duration === 0) {
      return;
    }
    const animations = specifications.map(({ element, keyframes, easing = "ease-in-out" }) => {
      const animation = element.animate(keyframes, { duration, easing, fill: "both" });
      animation.updatePlaybackRate(this.speed);
      if (this.paused) animation.pause();
      this.activeAnimations.add(animation);
      return animation;
    });
    const cancel = () => {
      for (const animation of animations) animation.cancel();
    };
    signal.addEventListener("abort", cancel, { once: true });
    try {
      await Promise.all(animations.map((animation) => animation.finished));
    } finally {
      signal.removeEventListener("abort", cancel);
      for (const animation of animations) {
        this.activeAnimations.delete(animation);
        animation.cancel();
      }
    }
  }

  wait(duration, signal) {
    if (signal.aborted || REDUCED_MOTION.matches || duration === 0) {
      return Promise.resolve();
    }
    return new Promise((resolve, reject) => {
      let remaining = duration;
      let previous = performance.now();
      let timer = 0;
      const abort = () => {
        clearTimeout(timer);
        reject(new DOMException("Playback reset", "AbortError"));
      };
      const tick = () => {
        const now = performance.now();
        if (!this.paused) {
          remaining -= (now - previous) * this.speed;
        }
        previous = now;
        if (remaining <= 0) {
          signal.removeEventListener("abort", abort);
          resolve();
          return;
        }
        timer = window.setTimeout(tick, 32);
      };
      signal.addEventListener("abort", abort, { once: true });
      timer = window.setTimeout(tick, 0);
    });
  }
}

function boatPosition(scene) {
  return scene.boat === "HOME" ? [40, 69] : [60, 69];
}

function farmerPosition(scene) {
  const [boatLeft, boatTop] = boatPosition(scene);
  return [boatLeft - 2.1, boatTop - 2.1];
}

function shorePosition(scene, name) {
  const side = scene[name.toLowerCase()];
  const positions = {
    HOME: {
      SHEEP: [17, 49],
      WOLF: [23, 68],
      HAY: [29, 54]
    },
    FAR: {
      SHEEP: [83, 49],
      WOLF: [77, 68],
      HAY: [71, 54]
    }
  };
  return positions[side][name];
}

function place(element, [left, top]) {
  element.style.left = `${left}%`;
  element.style.top = `${top}%`;
}

function localAnimationTranslation(element, stage, viewportX, viewportY) {
  let transform = new DOMMatrix();
  let ancestor = element.parentElement;
  while (ancestor !== stage) {
    if (ancestor === null) {
      throw new Error("River scene element is not owned by its stage");
    }
    const ancestorTransform = getComputedStyle(ancestor).transform;
    if (ancestorTransform !== "none") {
      transform = new DOMMatrix(ancestorTransform).multiply(transform);
    }
    ancestor = ancestor.parentElement;
  }
  const inverse = transform.inverse();
  const localX = inverse.a * viewportX + inverse.c * viewportY;
  const localY = inverse.b * viewportX + inverse.d * viewportY;
  if (!Number.isFinite(localX) || !Number.isFinite(localY)) {
    throw new Error("River scene transform is not invertible");
  }
  return [localX, localY];
}

function createSceneRenderer(game) {
  const stage = required("[data-river-stage]", game);
  const boat = required("[data-river-boat]", game);
  const boatHull = required("[data-river-boat-hull]", game);
  const speech = required("[data-river-speech]", game);
  const actors = new Map(
    [...game.querySelectorAll("[data-river-character]")]
      .map((element) => [element.dataset.riverCharacter, element])
  );
  const dust = required("[data-river-dust]", game);

  function render(scene) {
    place(boat, boatPosition(scene));
    place(boatHull, boatPosition(scene));
    place(speech, farmerPosition(scene));
    speech.dataset.side = scene.farmer;
    for (const [name, actor] of actors) {
      const aboard = scene[name.toLowerCase()] === "BOAT";
      if (aboard) {
        if (actor.parentElement !== boat) {
          boat.append(actor);
        }
        actor.classList.add("is-aboard");
        place(actor, [72, 62.5]);
      } else {
        if (actor.parentElement !== stage) {
          stage.append(actor);
        }
        actor.classList.remove("is-aboard");
        place(actor, shorePosition(scene, name));
      }
      actor.classList.remove("is-eaten", "is-licking");
    }
    dust.classList.remove("is-active");
    delete stage.dataset.dangerSide;
    if (scene.danger) {
      const predator = actors.get(scene.danger.predator);
      const prey = actors.get(scene.danger.prey);
      const side = scene[scene.danger.predator.toLowerCase()];
      stage.dataset.dangerSide = side;
      dust.classList.add("is-active");
      prey.classList.add("is-eaten");
    }
    stage.dataset.boatSide = scene.boat;
    stage.dataset.boatHeading = scene.boatHeading;
    stage.dataset.sceneComplete = String(scene.complete);
  }

  function elements() {
    return [boat, boatHull, ...actors.values()];
  }

  return { actors, boatElements: [boat, boatHull], dust, elements, render, stage };
}

async function transitionScene(renderer, controller, currentScene, event, duration, signal) {
  const before = new Map(renderer.elements().map((element) => [element, element.getBoundingClientRect()]));
  const nextScene = applyRiverEvent(currentScene, event);
  renderer.render(nextScene);
  const specifications = [];
  for (const element of renderer.elements()) {
    if (event.action === "CROSS" && element.classList.contains("is-aboard")) {
      continue;
    }
    const start = before.get(element);
    const finish = element.getBoundingClientRect();
    const x = start.left + start.width / 2 - (finish.left + finish.width / 2);
    const y = start.top + start.height / 2 - (finish.top + finish.height / 2);
    if (Math.abs(x) > 0.5 || Math.abs(y) > 0.5) {
      const [localX, localY] = localAnimationTranslation(element, renderer.stage, x, y);
      specifications.push({
        element,
        keyframes: [{ translate: `${localX}px ${localY}px` }, { translate: "0 0" }],
        easing: event.action === "CROSS" ? "cubic-bezier(.45,.05,.55,.95)" : "cubic-bezier(.2,.8,.2,1)"
      });
    }
  }
  await controller.animate(specifications, duration, signal);
  return nextScene;
}

export async function initializeRiverChallenge(section, {
  analyzeDynLex,
  completeDynLex,
  music,
  runDynLex
}) {
  if (typeof runDynLex !== "function") {
    throw new TypeError("River challenge requires a DynLex runner");
  }
  if (typeof analyzeDynLex !== "function") {
    throw new TypeError("River challenge requires a DynLex language analyzer");
  }
  if (typeof completeDynLex !== "function") {
    throw new TypeError("River challenge requires DynLex completion");
  }
  await Promise.all([
    loadImage(LANDSCAPE_URL),
    loadImage(BOAT_URL),
    loadImage(BOAT_BLINK_URL),
    loadImage(SHEEP_URL),
    loadImage(SHEEP_BLINK_URL),
    loadImage(WOLF_URL),
    loadImage(WOLF_BLINK_URL),
    loadImage(HAY_URL)
  ]);

  const mount = required("[data-river-challenge-mount]", section);
  mount.innerHTML = riverMarkup();
  const game = required("[data-river-game]", mount);
  required('[data-river-character="WOLF"]', game).append(createWolfTongue());
  const source = required("[data-river-source]", game);
  const highlight = required("[data-river-source-highlight]", game);
  const sourceCode = required("[data-river-source-code]", game);
  const playbackHighlight = required("[data-river-source-playback]", game);
  const callCode = required("[data-river-call-code]", game);
  const diagnosticHighlight = required("[data-river-source-diagnostics]", game);
  const diagnosticCode = required("[data-river-diagnostic-code]", game);
  const completionList = required("[data-river-completions]", game);
  const lineIndicator = required("[data-river-source-line-state]", game);
  const editorShell = required("[data-river-editor-shell]", game);
  const diagnostics = required("[data-river-diagnostics]", game);
  const runButton = required("[data-river-run]", game);
  const playButton = required("[data-river-playback-toggle]", game);
  const speedButton = required("[data-river-speed]", game);
  const resetButton = required("[data-river-reset]", game);
  const muteButton = required("[data-river-mute]", game);
  const status = required("[data-river-status]", game);
  const worldState = required("[data-river-world-state]", game);
  const message = required("[data-river-message]", game);
  const speech = required("[data-river-speech]", game);
  const speechText = required("[data-river-speech-text]", game);
  const renderer = createSceneRenderer(game);
  const sheep = renderer.actors.get("SHEEP");
  const challengeAudio = await createRiverChallengeAudio(section, music, muteButton, sheep);
  let scene = createInitialRiverScene();
  let lastTrace = null;
  let lastCallRanges = [];
  let playing = false;
  let programGeneration = 0;
  let highlightGeneration = 0;
  let highlightTimer = null;
  let activeEditLine = null;

  function scheduleSemanticHighlight({ delay = 160, line = null, position } = {}) {
    highlightGeneration += 1;
    const generation = highlightGeneration;
    if (highlightTimer !== null) {
      clearTimeout(highlightTimer);
    }
    const sourceText = source.value;
    if (line === null) {
      if (sourceCode.textContent !== sourceText) {
        applyRiverSourceEdit(sourceCode, sourceCode.textContent, sourceText);
      }
    } else {
      renderRiverSourceLine(sourceCode, sourceCode.textContent, sourceText, [], line);
    }
    editorShell.dataset.highlightState = "loading";
    highlightTimer = window.setTimeout(() => {
      highlightTimer = null;
      void analyzeDynLex(RIVER_PROGRAM_PREFIX + sourceText, position).then((feedback) => {
        if (generation !== highlightGeneration || sourceText !== source.value) {
          return;
        }
        let sourceDiagnostics = [];
        if (line === null) {
          sourceDiagnostics = renderRiverLspFeedback({
            diagnosticCode,
            diagnostics: feedback.diagnostics,
            lineIndicator,
            panel: diagnostics,
            source: sourceText,
            sourceCode,
            semanticTokens: feedback.semanticTokens
          });
        } else {
          renderRiverLspLineFeedback({
            line,
            previousSource: sourceCode.textContent,
            source: sourceText,
            sourceCode,
            semanticTokens: feedback.semanticTokens
          });
        }
        editorShell.dataset.highlightState = "semantic";
        if (sourceDiagnostics.length > 0) {
          status.textContent = "CHECK CODE";
          worldState.textContent = "the plan does not compile";
          game.dataset.playbackState = "error";
        }
      }).catch((error) => {
        if (generation !== highlightGeneration) {
          return;
        }
        console.error("River challenge syntax highlighting failed", error);
        renderRiverDiagnosticRanges(diagnosticCode, source.value, []);
        diagnostics.hidden = false;
        diagnostics.textContent = "An error occurred. Check the browser log.";
        status.textContent = "ERROR";
        worldState.textContent = "the plan could not be analyzed";
        game.dataset.playbackState = "error";
        editorShell.dataset.highlightState = "error";
      });
    }, delay);
  }

  function commitEditedLine(position) {
    if (activeEditLine === null) return;
    if (
      position !== undefined
      && position.line - RIVER_PROGRAM_PREFIX_LINES === activeEditLine
    ) {
      return;
    }
    activeEditLine = null;
    scheduleSemanticHighlight({ delay: 0, position });
  }

  function syncEditorScroll() {
    highlight.scrollLeft = source.scrollLeft;
    highlight.scrollTop = source.scrollTop;
    playbackHighlight.scrollLeft = source.scrollLeft;
    playbackHighlight.scrollTop = source.scrollTop;
    diagnosticHighlight.scrollLeft = source.scrollLeft;
    diagnosticHighlight.scrollTop = source.scrollTop;
    editorShell.style.setProperty("--river-source-scroll-y", `${-source.scrollTop}px`);
  }

  function syncControls({ paused, speed }) {
    playButton.innerHTML = paused
      ? '<span aria-hidden="true">▶</span> PLAY'
      : '<span aria-hidden="true">Ⅱ</span> PAUSE';
    playButton.setAttribute("aria-label", paused ? "Play animation" : "Pause animation");
    speedButton.textContent = `${speed}× SPEED`;
    game.dataset.playbackState = playing ? (paused ? "paused" : "running") : game.dataset.playbackState;
    challengeAudio.setTracePlayback({ paused, speed });
  }

  const controller = new PlaybackController(renderer.stage, syncControls);

  function hideSpeech() {
    speech.hidden = true;
    speechText.textContent = "";
  }

  function resetScene({ clearFeedback = true } = {}) {
    programGeneration += 1;
    controller.stop();
    challengeAudio.stopTraceEffects();
    challengeAudio.setIdleEnabled(true);
    playing = false;
    scene = createInitialRiverScene();
    renderer.render(scene);
    hideSpeech();
    worldState.textContent = "waiting for a plan";
    status.textContent = "READY";
    game.dataset.playbackState = "ready";
    if (clearFeedback) {
      clearRiverLineStates(lineIndicator);
      renderRiverCallRange(callCode, source.value);
      diagnostics.hidden = true;
      diagnostics.textContent = "";
      message.textContent = "The farmer can take one passenger at a time.";
    }
    playButton.disabled = lastTrace === null;
    syncControls({ paused: true, speed: controller.speed });
  }

  async function animateDanger(event, signal) {
    scene = applyRiverEvent(scene, event);
    const predator = renderer.actors.get(event.predator);
    const prey = renderer.actors.get(event.prey);
    const side = scene[event.predator.toLowerCase()];
    renderer.stage.dataset.dangerSide = side;
    renderer.dust.classList.add("is-active");
    predator.classList.add("is-fighting");
    prey.classList.add("is-fighting");
    await controller.wait(650, signal);
    predator.classList.remove("is-fighting");
    prey.classList.remove("is-fighting");
    prey.classList.add("is-eaten");
    predator.classList.add("is-licking");
    const tongue = required(".river-wolf-tongue", predator);
    try {
      await controller.animate(
        createWolfTongueLickSpecifications(tongue),
        WOLF_TONGUE_LICK_DURATION,
        signal
      );
    } finally {
      predator.classList.remove("is-licking");
    }
  }

  async function playTrace(trace, callRanges) {
    const signal = controller.begin();
    challengeAudio.setIdleEnabled(false);
    playing = true;
    playButton.disabled = false;
    game.dataset.playbackState = "running";
    status.textContent = "RUNNING";
    worldState.textContent = "following your plan";
    message.textContent = "Every line changes what the next line can do.";
    hideSpeech();
    scene = createInitialRiverScene();
    renderer.render(scene);
    clearRiverLineStates(lineIndicator);
    renderRiverCallRange(callCode, source.value);

    try {
      for (let index = 0; index < trace.commands.length; index += 1) {
        const command = trace.commands[index];
        const callRange = callRanges[index];
        const lineNumber = callRange.start.line + 1;
        setRiverLineState(lineIndicator, lineNumber, "active");
        renderRiverCallRange(callCode, source.value, callRange, "active");
        for (const event of command.events) {
          if (event.type === "ACTION") {
            const duration = event.action === "CROSS" ? 1000 : 560;
            const sound = challengeAudio.playTraceEffect(
              event.action === "CROSS" ? "rowing" : "boat",
              signal
            );
            try {
              scene = await transitionScene(renderer, controller, scene, event, duration, signal);
            } finally {
              sound.stop();
            }
          } else if (event.type === "DANGER") {
            const sound = challengeAudio.playTraceEffect("anxious", signal);
            try {
              await animateDanger(event, signal);
            } finally {
              sound.stop();
            }
          } else if (event.type === "ERROR") {
            scene = applyRiverEvent(scene, event);
            setRiverLineState(lineIndicator, lineNumber, "error");
            renderRiverCallRange(callCode, source.value, callRange, "error");
            diagnostics.hidden = false;
            diagnostics.textContent = event.message;
            message.textContent = event.message;
            status.textContent = "TRY AGAIN";
            worldState.textContent = "the plan needs another idea";
            game.dataset.playbackState = "failure";
          } else if (event.type === "SUCCESS") {
            challengeAudio.playOneShot("win");
            scene = applyRiverEvent(scene, event);
            renderer.render(scene);
            speech.hidden = false;
            speech.dataset.side = "FAR";
            speechText.textContent = "We made it! What a brilliant plan!";
            message.textContent = "Everyone reached the far bank safely.";
            status.textContent = "SOLVED";
            worldState.textContent = "river crossed";
            await controller.animate([
              {
                element: speech,
                keyframes: [
                  { scale: 0.65, opacity: 0 },
                  { scale: 1, opacity: 1 }
                ],
                easing: "cubic-bezier(.2, 1.4, .4, 1)"
              },
              ...renderer.boatElements.map((element) => ({
                element,
                keyframes: [
                  { scale: 0.75, translate: "0 8px" },
                  { scale: 1.08, translate: "0 -5px", offset: 0.72 },
                  { scale: 1, translate: "0 0" }
                ],
                easing: "cubic-bezier(.2, 1.1, .4, 1)"
              }))
            ], 520, signal);
            game.dataset.playbackState = "success";
          }
        }
      }
      if (trace.outcome === "failure") {
        await controller.wait(2400, signal);
        scene = createInitialRiverScene();
        renderer.render(scene);
        hideSpeech();
        worldState.textContent = "paused — edit and try again";
      } else if (trace.outcome === "running") {
        status.textContent = "PLAN ENDED";
        worldState.textContent = "waiting for the next instruction";
        message.textContent = "Everyone is safe so far, but the crossing is not complete.";
        game.dataset.playbackState = "paused";
      }
    } catch (error) {
      if (error.name !== "AbortError") {
        throw error;
      }
    } finally {
      if (!signal.aborted) {
        playing = false;
        controller.paused = true;
        controller.sync();
        game.dataset.playbackState = trace.outcome === "running" ? "paused" : trace.outcome;
      }
      challengeAudio.stopTraceEffects();
      challengeAudio.setIdleEnabled(true);
    }
  }

  async function runProgram() {
    const generation = ++programGeneration;
    controller.stop();
    challengeAudio.stopTraceEffects();
    challengeAudio.setIdleEnabled(false);
    playing = false;
    clearRiverLineStates(lineIndicator);
    renderRiverCallRange(callCode, source.value);
    diagnostics.hidden = true;
    diagnostics.textContent = "";
    hideSpeech();
    runButton.disabled = true;
    status.textContent = "COMPILING";
    worldState.textContent = "reading your plan";
    game.dataset.playbackState = "compiling";
    try {
      const programSource = RIVER_PROGRAM_PREFIX + source.value;
      const result = await runDynLex(programSource);
      if (generation !== programGeneration) {
        return;
      }
      if (result.compileResult.status !== 0) {
        renderRiverCompilerDiagnostics(diagnostics, lineIndicator, result.compileResult.diagnostics);
        status.textContent = "CHECK CODE";
        worldState.textContent = "the plan did not compile";
        game.dataset.playbackState = "error";
        return;
      }
      diagnostics.hidden = true;
      diagnostics.textContent = "";
      if (result.runResult.stdout.trim().length === 0) {
        diagnostics.hidden = false;
        diagnostics.textContent = "Your plan has no river instructions yet.";
        status.textContent = "CHECK PLAN";
        worldState.textContent = "waiting for an instruction";
        game.dataset.playbackState = "error";
        return;
      }
      const trace = parseRiverTrace(result.runResult.stdout);
      const feedback = await analyzeDynLex(programSource);
      const callRanges = riverCommandCallRanges(feedback.callExpressions, trace.commands.length);
      lastTrace = trace;
      lastCallRanges = callRanges;
      await playTrace(trace, callRanges);
    } catch (error) {
      console.error("River challenge program failed", error);
      diagnostics.hidden = false;
      diagnostics.textContent = "An error occurred. Check the browser log.";
      status.textContent = "ERROR";
      worldState.textContent = "the plan could not run";
      game.dataset.playbackState = "error";
    } finally {
      runButton.disabled = false;
      if (!playing) {
        challengeAudio.setIdleEnabled(true);
      }
    }
  }

  const completions = createRiverCompletions({
    completeDynLex,
    list: completionList,
    source
  });
  let completionRequestQueued = false;
  function requestCompletions() {
    if (completionRequestQueued) {
      return;
    }
    completionRequestQueued = true;
    queueMicrotask(() => {
      completionRequestQueued = false;
      void completions.request().catch((error) => {
        console.error("River challenge completion failed", error);
        completions.close();
      });
    });
  }
  source.value = RIVER_STARTER_SOURCE;
  scheduleSemanticHighlight({
    delay: 0,
    position: prefixedRiverPosition(source.value, source.selectionEnd)
  });
  source.addEventListener("scroll", () => {
    syncEditorScroll();
    completions.close();
  }, { passive: true });
  source.addEventListener("input", () => {
    programGeneration += 1;
    controller.stop();
    challengeAudio.stopTraceEffects();
    challengeAudio.setIdleEnabled(true);
    playing = false;
    clearRiverLineStates(lineIndicator);
    renderRiverCallRange(callCode, source.value);
    renderRiverDiagnosticRanges(diagnosticCode, source.value, []);
    const position = prefixedRiverPosition(source.value, source.selectionEnd);
    const changedLine = riverSingleChangedLine(sourceCode.textContent, source.value);
    const cursorLine = riverSourcePosition(source.value, source.selectionEnd).line;
    if (
      changedLine === cursorLine
      && (activeEditLine === null || activeEditLine === changedLine)
    ) {
      activeEditLine = changedLine;
      scheduleSemanticHighlight({ line: changedLine, position });
    } else {
      activeEditLine = changedLine === cursorLine ? changedLine : null;
      scheduleSemanticHighlight({ delay: 0, position });
    }
    diagnostics.hidden = true;
    diagnostics.textContent = "";
    message.textContent = "Your previous result was cleared because the plan changed.";
    status.textContent = "EDITED";
    worldState.textContent = "ready for the new plan";
    game.dataset.playbackState = "edited";
    scene = createInitialRiverScene();
    renderer.render(scene);
    hideSpeech();
    lastTrace = null;
    lastCallRanges = [];
    playButton.disabled = true;
    requestCompletions();
  });
  source.addEventListener("keydown", (event) => {
    if (completions.handleKeydown(event)) {
      return;
    }
    completions.close();
    if (event.key === "Tab") {
      event.preventDefault();
      source.setRangeText("    ", source.selectionStart, source.selectionEnd, "end");
      source.dispatchEvent(new Event("input", { bubbles: true }));
    } else if (event.key === "Enter" && (event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      if (!runButton.disabled) void runProgram();
    }
  });
  source.addEventListener("pointerdown", () => completions.close());
  source.addEventListener("selectionchange", () => {
    if (source.ownerDocument.activeElement !== source) return;
    const position = prefixedRiverPosition(source.value, source.selectionEnd);
    if (position.line - RIVER_PROGRAM_PREFIX_LINES !== activeEditLine) {
      completions.close();
    }
    commitEditedLine(position);
  });
  source.addEventListener("blur", () => {
    commitEditedLine(undefined);
    window.setTimeout(() => completions.close(), 0);
  });
  runButton.addEventListener("click", () => void runProgram());
  playButton.addEventListener("click", () => {
    if (playing) {
      controller.toggle();
      return;
    }
    if (lastTrace) {
      void playTrace(lastTrace, lastCallRanges);
    }
  });
  speedButton.addEventListener("click", () => controller.cycleSpeed());
  resetButton.addEventListener("click", () => resetScene());
  editorShell.addEventListener("click", (event) => {
    if (event.target !== source) {
      source.focus();
    }
  });
  resetScene();
  mount.dataset.challengeLoaded = "true";
}
