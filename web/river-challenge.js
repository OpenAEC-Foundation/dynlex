import {
  applyRiverEvent,
  createInitialRiverScene,
  parseRiverTrace
} from "./river-challenge-model.js";
import {
  rebaseLspDiagnosticsAfterLines,
  rebaseSemanticTokensAfterLines,
  renderSemanticTokens
} from "./semantic-highlighting.js";
import { semanticTokenLegend } from "./semantic-token-legend.js";
import { createRiverChallengeAudio } from "./river-challenge-audio.js";

const LANDSCAPE_URL = new URL("./media/river-challenge/painted-river.webp", import.meta.url).href;
const BOAT_URL = new URL("./media/river-challenge/boat.webp", import.meta.url).href;
const BOAT_BLINK_URL = new URL("./media/river-challenge/boat-blink.webp", import.meta.url).href;
const SHEEP_URL = new URL("./media/river-challenge/sheep.webp", import.meta.url).href;
const SHEEP_BLINK_URL = new URL("./media/river-challenge/sheep-blink.webp", import.meta.url).href;
const WOLF_URL = new URL("./media/river-challenge/wolf.webp", import.meta.url).href;
const WOLF_BLINK_URL = new URL("./media/river-challenge/wolf-blink.webp", import.meta.url).href;
const HAY_URL = new URL("./media/river-challenge/hay.webp", import.meta.url).href;
const PROGRAM_PREFIX = "import lib/river_challenge.dl\n\n";
const PROGRAM_PREFIX_LINES = 2;
const STARTER_SOURCE = "# The official names are: sheep, wolf, and hay.\nget the hay in the boat\nrow to the other side";
const SPEEDS = [1, 2, 4];
const SUBJECTS = ["SHEEP", "WOLF", "HAY"];
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
            <pre class="river-source-diagnostics" data-river-source-diagnostics aria-hidden="true"><code data-river-diagnostic-code></code></pre>
            <textarea
              class="river-source"
              data-river-source
              aria-label="River crossing DynLex source"
              spellcheck="false"
              wrap="off"
              autocapitalize="off"
              autocomplete="off"
            ></textarea>
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
            <div class="river-character river-wolf" data-river-character="WOLF" role="img" aria-label="Wolf"><i class="river-blink-layer river-wolf-blink" aria-hidden="true"></i><i class="river-wolf-tongue" aria-hidden="true"></i></div>
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

function commandFromLine(line) {
  const source = line.trim().toLowerCase();
  if (source.length === 0 || source.startsWith("#")) {
    return null;
  }
  if (/^(?:row|cross)\b/.test(source)) {
    return { action: "CROSS", subject: null };
  }
  const subject = SUBJECTS.find((name) => new RegExp(`\\b${name.toLowerCase()}\\b`).test(source));
  if (/^(?:unload|let)\b/.test(source) || /\bout(?:\s+of\s+the\s+boat)?$/.test(source)) {
    return { action: "UNLOAD", subject: subject ?? null };
  }
  if (/^(?:get|put|load|take)\b/.test(source)) {
    return { action: "LOAD", subject: subject ?? null };
  }
  return null;
}

function commandLineNumbers(source, commands) {
  const sourceCommands = source.split("\n")
    .map((line, index) => ({ command: commandFromLine(line), line: index + 1 }))
    .filter(({ command }) => command !== null);
  const mapped = [];
  let sourceIndex = 0;
  for (const command of commands) {
    let matchedIndex = -1;
    for (let offset = 0; offset < sourceCommands.length; offset += 1) {
      const candidateIndex = (sourceIndex + offset) % sourceCommands.length;
      const candidate = sourceCommands[candidateIndex];
      if (
        candidate.command.action === command.action
        && (
          candidate.command.subject === null
          || candidate.command.subject === command.subject
        )
      ) {
        mapped.push(candidate.line);
        matchedIndex = candidateIndex;
        break;
      }
    }
    if (matchedIndex === -1) {
      throw new Error("Compiled river command could not be mapped to its source line");
    }
    sourceIndex = (matchedIndex + 1) % sourceCommands.length;
  }
  return mapped;
}

function renderSource(code, source, tokenData) {
  renderSemanticTokens(code, source, tokenData, semanticTokenLegend, {
    baseClass: "river-token",
    classPrefix: "river-token-"
  });
}

function sourceLineStarts(source) {
  const starts = [0];
  for (let index = 0; index < source.length; index += 1) {
    if (source[index] === "\n") {
      starts.push(index + 1);
    }
  }
  return starts;
}

function sourceOffset(source, lineStarts, position) {
  if (position.line >= lineStarts.length) {
    throw new Error("LSP diagnostic line points outside the river source");
  }
  const start = lineStarts[position.line];
  const end = position.line + 1 < lineStarts.length
    ? lineStarts[position.line + 1] - 1
    : source.length;
  if (position.character > end - start) {
    throw new Error("LSP diagnostic column points outside the river source");
  }
  return start + position.character;
}

function diagnosticSourceRanges(source, diagnostics) {
  const lineStarts = sourceLineStarts(source);
  const ranges = diagnostics.map((diagnostic) => {
    let start = sourceOffset(source, lineStarts, diagnostic.range.start);
    let end = sourceOffset(source, lineStarts, diagnostic.range.end);
    if (start === end) {
      if (end < source.length && source[end] !== "\n") {
        end += 1;
      } else if (start > 0 && source[start - 1] !== "\n") {
        start -= 1;
      }
    }
    return { start, end };
  }).sort((left, right) => left.start - right.start || left.end - right.end);

  const merged = [];
  for (const range of ranges) {
    const previous = merged.at(-1);
    if (previous && range.start <= previous.end) {
      previous.end = Math.max(previous.end, range.end);
    } else {
      merged.push(range);
    }
  }
  return merged;
}

function renderDiagnosticRanges(code, source, diagnostics) {
  const fragment = code.ownerDocument.createDocumentFragment();
  let offset = 0;
  for (const range of diagnosticSourceRanges(source, diagnostics)) {
    if (range.start > offset) {
      fragment.append(code.ownerDocument.createTextNode(source.slice(offset, range.start)));
    }
    const marker = code.ownerDocument.createElement("span");
    marker.dataset.riverDiagnosticRange = "";
    marker.textContent = source.slice(range.start, range.end);
    if (range.start === range.end) {
      marker.classList.add("is-empty");
      marker.textContent = "\u00a0";
    }
    fragment.append(marker);
    offset = range.end;
  }
  if (offset < source.length) {
    fragment.append(code.ownerDocument.createTextNode(source.slice(offset)));
  }
  code.replaceChildren(fragment);
}

function setLineState(indicator, lineNumber, state) {
  if (!Number.isInteger(lineNumber) || lineNumber < 1) {
    throw new Error("River source line must be a positive integer");
  }
  indicator.hidden = false;
  indicator.dataset.riverSourceLine = String(lineNumber);
  indicator.dataset.riverLineState = state;
  indicator.style.setProperty("--river-source-line-index", String(lineNumber - 1));
}

function clearLineStates(indicator) {
  indicator.hidden = true;
  delete indicator.dataset.riverSourceLine;
  delete indicator.dataset.riverLineState;
  indicator.style.removeProperty("--river-source-line-index");
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
      predator.classList.add("is-licking");
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

function renderCompilerDiagnostics(panel, lineIndicator, diagnostics) {
  panel.replaceChildren();
  clearLineStates(lineIndicator);
  if (diagnostics.length === 0) {
    panel.hidden = true;
    return;
  }
  panel.hidden = false;
  const messages = [];
  for (const diagnostic of diagnostics) {
    const compilerLine = Number.isInteger(diagnostic.line) ? diagnostic.line : 1;
    const sourceLine = compilerLine - PROGRAM_PREFIX_LINES;
    if (sourceLine < 1) {
      throw new Error(`River challenge library diagnostic: ${diagnostic.message}`);
    }
    setLineState(lineIndicator, sourceLine, "error");
    const column = Number.isInteger(diagnostic.column) ? diagnostic.column : 1;
    messages.push(`ERROR ${sourceLine}:${column}  ${diagnostic.message}`);
  }
  panel.textContent = messages.join("\n");
}

function renderLspDiagnostics(panel, lineIndicator, diagnosticCode, source, diagnostics) {
  panel.replaceChildren();
  renderDiagnosticRanges(diagnosticCode, source, diagnostics);
  if (diagnostics.length === 0) {
    panel.hidden = true;
    return;
  }

  clearLineStates(lineIndicator);
  const severityLabels = new Map([
    [1, "ERROR"],
    [2, "WARNING"],
    [3, "INFO"],
    [4, "HINT"]
  ]);
  panel.hidden = false;
  panel.textContent = diagnostics.map((diagnostic) => {
    const line = diagnostic.range.start.line + 1;
    const column = diagnostic.range.start.character + 1;
    setLineState(lineIndicator, line, "error");
    return `${severityLabels.get(diagnostic.severity) ?? "ERROR"} ${line}:${column}  ${diagnostic.message}`;
  }).join("\n");
}

export async function initializeRiverChallenge(section, { analyzeDynLex, music, runDynLex }) {
  if (typeof runDynLex !== "function") {
    throw new TypeError("River challenge requires a DynLex runner");
  }
  if (typeof analyzeDynLex !== "function") {
    throw new TypeError("River challenge requires a DynLex language analyzer");
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
  const source = required("[data-river-source]", game);
  const highlight = required("[data-river-source-highlight]", game);
  const sourceCode = required("[data-river-source-code]", game);
  const diagnosticHighlight = required("[data-river-source-diagnostics]", game);
  const diagnosticCode = required("[data-river-diagnostic-code]", game);
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
  let lastLineNumbers = [];
  let playing = false;
  let programGeneration = 0;
  let highlightGeneration = 0;
  let highlightTimer = null;

  function scheduleSemanticHighlight(delay = 160) {
    highlightGeneration += 1;
    const generation = highlightGeneration;
    if (highlightTimer !== null) {
      clearTimeout(highlightTimer);
    }
    renderSource(sourceCode, source.value, []);
    renderDiagnosticRanges(diagnosticCode, source.value, []);
    editorShell.dataset.highlightState = "loading";
    highlightTimer = window.setTimeout(() => {
      highlightTimer = null;
      const sourceText = source.value;
      void analyzeDynLex(PROGRAM_PREFIX + sourceText).then((feedback) => {
        if (generation !== highlightGeneration || sourceText !== source.value) {
          return;
        }
        const sourceTokens = rebaseSemanticTokensAfterLines(
          feedback.semanticTokens,
          PROGRAM_PREFIX_LINES
        );
        const sourceDiagnostics = rebaseLspDiagnosticsAfterLines(
          feedback.diagnostics,
          PROGRAM_PREFIX_LINES
        );
        renderSource(sourceCode, sourceText, sourceTokens);
        renderLspDiagnostics(
          diagnostics,
          lineIndicator,
          diagnosticCode,
          sourceText,
          sourceDiagnostics
        );
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
        renderSource(sourceCode, source.value, []);
        renderDiagnosticRanges(diagnosticCode, source.value, []);
        diagnostics.hidden = false;
        diagnostics.textContent = "An error occurred. Check the browser log.";
        status.textContent = "ERROR";
        worldState.textContent = "the plan could not be analyzed";
        game.dataset.playbackState = "error";
        editorShell.dataset.highlightState = "error";
      });
    }, delay);
  }

  function syncEditorScroll() {
    highlight.scrollLeft = source.scrollLeft;
    highlight.scrollTop = source.scrollTop;
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
      clearLineStates(lineIndicator);
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
    await controller.wait(350, signal);
  }

  async function playTrace(trace, lineNumbers) {
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
    clearLineStates(lineIndicator);

    try {
      for (let index = 0; index < trace.commands.length; index += 1) {
        const command = trace.commands[index];
        const lineNumber = lineNumbers[index];
        setLineState(lineIndicator, lineNumber, "active");
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
            setLineState(lineIndicator, lineNumber, "error");
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
    clearLineStates(lineIndicator);
    diagnostics.hidden = true;
    diagnostics.textContent = "";
    hideSpeech();
    runButton.disabled = true;
    status.textContent = "COMPILING";
    worldState.textContent = "reading your plan";
    game.dataset.playbackState = "compiling";
    try {
      const result = await runDynLex(PROGRAM_PREFIX + source.value);
      if (generation !== programGeneration) {
        return;
      }
      if (result.compileResult.status !== 0) {
        renderCompilerDiagnostics(diagnostics, lineIndicator, result.compileResult.diagnostics);
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
      const lineNumbers = commandLineNumbers(source.value, trace.commands);
      lastTrace = trace;
      lastLineNumbers = lineNumbers;
      await playTrace(trace, lineNumbers);
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

  source.value = STARTER_SOURCE;
  scheduleSemanticHighlight(0);
  source.addEventListener("scroll", syncEditorScroll, { passive: true });
  source.addEventListener("input", () => {
    programGeneration += 1;
    controller.stop();
    challengeAudio.stopTraceEffects();
    challengeAudio.setIdleEnabled(true);
    playing = false;
    clearLineStates(lineIndicator);
    renderDiagnosticRanges(diagnosticCode, source.value, []);
    scheduleSemanticHighlight();
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
    lastLineNumbers = [];
    playButton.disabled = true;
  });
  source.addEventListener("keydown", (event) => {
    if (event.key === "Tab") {
      event.preventDefault();
      source.setRangeText("    ", source.selectionStart, source.selectionEnd, "end");
      source.dispatchEvent(new Event("input", { bubbles: true }));
    } else if (event.key === "Enter" && (event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      if (!runButton.disabled) void runProgram();
    }
  });
  runButton.addEventListener("click", () => void runProgram());
  playButton.addEventListener("click", () => {
    if (playing) {
      controller.toggle();
      return;
    }
    if (lastTrace) {
      void playTrace(lastTrace, lastLineNumbers);
    }
  });
  speedButton.addEventListener("click", () => controller.cycleSpeed());
  resetButton.addEventListener("click", () => resetScene());
  editorShell.addEventListener("click", () => source.focus());
  resetScene();
  mount.dataset.challengeLoaded = "true";
}
