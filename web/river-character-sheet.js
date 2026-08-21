import {
  createWolfTongue,
  createWolfTongueLickSpecifications,
  WOLF_TONGUE_LICK_DURATION
} from "./river-character-art.js";

const SHEET_GROUPS = Object.freeze([
  {
    character: "BOAT",
    title: "Farmer + boat",
    note: "The farmer is baked into the boat sprite.",
    states: [
      { state: "idle", title: "Idle / blink loop", mode: "live", detail: ".river-boat-farmer-blink" },
      { state: "blink", title: "Blink frame", mode: "frame", detail: "forced eyelid overlay" },
      { state: "home-facing", title: "Facing home", mode: "frame", detail: "production horizontal flip" }
    ]
  },
  {
    character: "SHEEP",
    title: "Sheep",
    note: "Blink, anxious bellow, danger scuffle, and disappearance.",
    states: [
      { state: "idle", title: "Idle / blink loop", mode: "live", detail: ".river-sheep-blink" },
      { state: "blink", title: "Blink frame", mode: "frame", detail: "forced eyelid overlay" },
      { state: "bellowing", title: "Bellowing", mode: "frame", detail: ".is-bellowing" },
      { state: "scuffle", title: "Danger scuffle", mode: "live", detail: ".is-fighting" },
      { state: "eaten", title: "Eaten", mode: "final", detail: ".is-eaten" }
    ]
  },
  {
    character: "WOLF",
    title: "Wolf",
    note: "Blink, danger scuffle, and the post-fight one-shot lick (looped here for inspection).",
    states: [
      { state: "idle", title: "Idle / blink loop", mode: "live", detail: ".river-wolf-blink" },
      { state: "blink", title: "Blink frame", mode: "frame", detail: "forced eyelid overlay" },
      { state: "left-eyelid", title: "Left eyelid isolated", mode: "frame", detail: "::before only" },
      { state: "right-eyelid", title: "Right eyelid isolated", mode: "frame", detail: "::after only" },
      { state: "scuffle", title: "Danger scuffle", mode: "live", detail: ".is-fighting" },
      { state: "licking", title: "Licking", mode: "live", detail: ".is-licking / green = mouth line" }
    ]
  },
  {
    character: "HAY",
    title: "Hay",
    note: "The hay has no face or idle animation.",
    states: [
      { state: "idle", title: "Idle", mode: "frame", detail: "static production sprite" },
      { state: "scuffle", title: "Danger scuffle", mode: "live", detail: ".is-fighting" },
      { state: "eaten", title: "Eaten", mode: "final", detail: ".is-eaten" }
    ]
  }
]);

function required(selector, root = document) {
  const element = root.querySelector(selector);
  if (element === null) throw new Error(`Missing river sheet element: ${selector}`);
  return element;
}

function createBlinkLayer(className) {
  const blink = document.createElement("i");
  blink.className = `river-blink-layer ${className}`;
  blink.setAttribute("aria-hidden", "true");
  return blink;
}

function createBoat() {
  const boat = document.createElement("div");
  boat.className = "river-boat";
  boat.setAttribute("role", "img");
  boat.setAttribute("aria-label", "Boat with farmer");
  const image = document.createElement("img");
  image.src = "media/river-challenge/boat.webp";
  image.alt = "";
  boat.append(image, createBlinkLayer("river-boat-farmer-blink"));
  return boat;
}

function createWolfMouthLine() {
  const namespace = "http://www.w3.org/2000/svg";
  const line = document.createElementNS(namespace, "svg");
  line.classList.add("river-wolf-mouth-line");
  line.setAttribute("viewBox", "0 0 750 750");
  line.setAttribute("aria-hidden", "true");
  for (const pathData of [
    "M254 450 C278 500 329 529 382 520 C412 518 432 501 444 480",
    "M382 520 C390 513 393 503 392 492"
  ]) {
    const path = document.createElementNS(namespace, "path");
    path.setAttribute("d", pathData);
    line.append(path);
  }
  return line;
}

function createCharacter(character, state) {
  const name = character.toLowerCase();
  const actor = document.createElement("div");
  actor.className = `river-character river-${name}`;
  actor.dataset.riverCharacter = character;
  actor.setAttribute("role", "img");
  actor.setAttribute("aria-label", character[0] + name.slice(1));
  if (character === "SHEEP") {
    actor.append(createBlinkLayer("river-sheep-blink"));
    const mouth = document.createElement("i");
    mouth.className = "river-sheep-mouth";
    mouth.setAttribute("aria-hidden", "true");
    actor.append(mouth);
  } else if (character === "WOLF") {
    actor.append(createBlinkLayer("river-wolf-blink"));
    actor.append(createWolfTongue());
    if (state === "licking") actor.append(createWolfMouthLine());
  }
  return actor;
}

function createStateCard(character, stateDefinition) {
  const card = document.createElement("article");
  card.className = "river-sheet-card";
  card.dataset.character = character;
  card.dataset.state = stateDefinition.state;
  card.dataset.mode = stateDefinition.mode;

  const viewport = document.createElement("div");
  viewport.className = "river-stage river-sheet-viewport";
  viewport.dataset.boatHeading = stateDefinition.state === "home-facing" ? "HOME" : "FAR";
  const isolatedEyelid = stateDefinition.state.endsWith("-eyelid");
  if (stateDefinition.state === "blink" || isolatedEyelid) {
    viewport.classList.add("river-sheet-frame-blink");
  }
  if (isolatedEyelid) viewport.classList.add("river-sheet-isolated-eyelid");

  const actor = character === "BOAT" ? createBoat() : createCharacter(character, stateDefinition.state);
  if (stateDefinition.state === "bellowing") actor.classList.add("is-bellowing");
  if (stateDefinition.state === "scuffle") actor.classList.add("is-fighting");
  if (stateDefinition.state === "licking") actor.classList.add("is-licking");
  if (stateDefinition.state === "eaten") actor.classList.add("is-eaten");
  viewport.append(actor);

  if (stateDefinition.state === "eaten") {
    const finalLabel = document.createElement("span");
    finalLabel.className = "river-sheet-final-label";
    finalLabel.textContent = "opacity 0 + blur 5px";
    viewport.append(finalLabel);
  }

  const caption = document.createElement("div");
  caption.className = "river-sheet-caption";
  const heading = document.createElement("strong");
  heading.textContent = stateDefinition.title;
  const mode = document.createElement("span");
  mode.className = `river-sheet-mode is-${stateDefinition.mode}`;
  mode.textContent = stateDefinition.mode;
  const detail = document.createElement("code");
  detail.textContent = stateDefinition.detail;
  caption.append(heading, mode, detail);
  card.append(viewport, caption);
  return card;
}

function renderSheet(cast) {
  const fragment = document.createDocumentFragment();
  for (const group of SHEET_GROUPS) {
    const section = document.createElement("section");
    section.className = "river-sheet-character-group";
    section.dataset.characterGroup = group.character;
    const heading = document.createElement("header");
    const title = document.createElement("h2");
    title.textContent = group.title;
    const note = document.createElement("p");
    note.textContent = group.note;
    heading.append(title, note);
    const cards = document.createElement("div");
    cards.className = "river-sheet-state-list";
    cards.append(...group.states.map((state) => createStateCard(group.character, state)));
    section.append(heading, cards);
    fragment.append(section);
  }
  cast.append(fragment);
}

const sheet = required("[data-river-character-sheet]");
renderSheet(required("[data-river-sheet-cast]", sheet));
const sheetTongue = required('[data-character="WOLF"][data-state="licking"] .river-wolf-tongue', sheet);
for (const { element, keyframes, easing } of createWolfTongueLickSpecifications(sheetTongue)) {
  element.animate(keyframes, {
    duration: WOLF_TONGUE_LICK_DURATION,
    easing,
    fill: "both",
    iterations: Infinity
  });
}
const playback = required("[data-river-sheet-playback]", sheet);
const restart = required("[data-river-sheet-restart]", sheet);
const scrubber = required("[data-river-sheet-scrubber]", sheet);
const progress = required("[data-river-sheet-progress]", sheet);
const boxes = required("[data-river-sheet-boxes]", sheet);
let paused = false;

function animations() {
  return sheet.getAnimations({ subtree: true });
}

function setPaused(nextPaused) {
  paused = nextPaused;
  for (const animation of animations()) {
    if (paused) animation.pause();
    else animation.play();
  }
  playback.textContent = paused ? "PLAY LOOPS" : "PAUSE LOOPS";
  playback.setAttribute("aria-pressed", String(paused));
}

function scrubAnimations(percent) {
  setPaused(true);
  const ratio = percent / 100;
  for (const animation of animations()) {
    const duration = animation.effect?.getComputedTiming().duration;
    if (typeof duration !== "number" || !Number.isFinite(duration)) {
      throw new Error("River sheet animation has no finite iteration duration");
    }
    animation.currentTime = duration * ratio;
  }
  progress.textContent = `${percent}%`;
}

playback.addEventListener("click", () => setPaused(!paused));
restart.addEventListener("click", () => {
  for (const animation of animations()) animation.currentTime = 0;
  scrubber.value = "0";
  progress.textContent = "0%";
  setPaused(false);
});
scrubber.addEventListener("input", () => scrubAnimations(Number(scrubber.value)));
boxes.addEventListener("change", () => {
  sheet.dataset.overlayBoxes = String(boxes.checked);
});

document.documentElement.dataset.riverSheetReady = "true";
