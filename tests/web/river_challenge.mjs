import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const projectDir = path.resolve(import.meta.dirname, "../..");
const webDir = path.join(projectDir, "web");
const paths = {
  html: path.join(webDir, "index.html"),
  homepage: path.join(webDir, "homepage.js"),
  challenge: path.join(webDir, "river-challenge.js"),
  audio: path.join(webDir, "river-challenge-audio.js"),
  model: path.join(webDir, "river-challenge-model.js"),
  styles: path.join(webDir, "river-challenge.css"),
  homepageStyles: path.join(webDir, "sections.css"),
  library: path.join(projectDir, "lib/river_challenge.dl"),
  landscape: path.join(webDir, "media/river-challenge/painted-river.webp"),
  sheep: path.join(webDir, "media/river-challenge/sheep.webp"),
  sheepBlink: path.join(webDir, "media/river-challenge/sheep-blink.webp"),
  wolf: path.join(webDir, "media/river-challenge/wolf.webp"),
  wolfBlink: path.join(webDir, "media/river-challenge/wolf-blink.webp"),
  hay: path.join(webDir, "media/river-challenge/hay.webp"),
  boat: path.join(webDir, "media/river-challenge/boat.webp"),
  boatBlink: path.join(webDir, "media/river-challenge/boat-blink.webp"),
  music: path.join(webDir, "media/river-challenge/puzzle-casual-game-music.mp3"),
  ambience: path.join(webDir, "media/river-challenge/forest-river-ambience-loop.mp3"),
  boatSound: path.join(webDir, "media/river-challenge/boat-whoosh.mp3"),
  sheepIdleSound: path.join(webDir, "media/river-challenge/sheep-idle.mp3"),
  sheepAnxiousSound: path.join(webDir, "media/river-challenge/sheep-anxious.mp3"),
  rowingSound: path.join(webDir, "media/river-challenge/rowing-paddle.mp3"),
  winSound: path.join(webDir, "media/river-challenge/win-level-up.mp3"),
  attributions: path.join(webDir, "wiki/attributions.html")
};

for (const [name, filePath] of Object.entries(paths)) {
  assert.ok(fs.existsSync(filePath), `Missing river challenge ${name}: ${path.relative(projectDir, filePath)}`);
}

for (const filePath of [paths.challenge, paths.audio, paths.model, paths.styles, paths.library]) {
  const lineCount = fs.readFileSync(filePath, "utf8").split("\n").length;
  assert.ok(lineCount < 1000, `${path.relative(projectDir, filePath)} must stay under 1000 lines`);
}

const html = fs.readFileSync(paths.html, "utf8");
const homepage = fs.readFileSync(paths.homepage, "utf8");
const challenge = fs.readFileSync(paths.challenge, "utf8");
const audio = fs.readFileSync(paths.audio, "utf8");
const styles = fs.readFileSync(paths.styles, "utf8");
const homepageStyles = fs.readFileSync(paths.homepageStyles, "utf8");
const library = fs.readFileSync(paths.library, "utf8");
const attributions = fs.readFileSync(paths.attributions, "utf8");
const oldCharacterAtlas = path.join(webDir, "media/river-challenge/characters.webp");

assert.equal(
  fs.existsSync(oldCharacterAtlas),
  false,
  "The superseded character atlas with its standalone farmer sprite must be removed"
);

assert.match(html, /data-river-challenge/);
assert.match(html, /data-river-challenge-load/);
assert.doesNotMatch(html, /<audio\b/i, "Challenge music must not load before opt-in");
assert.doesNotMatch(
  html,
  /forest-river-ambience|boat-whoosh|sheep-idle|sheep-anxious|rowing-paddle|win-level-up/
);
assert.match(html, /river-challenge\.css/);
assert.match(html, /data-river-preview/);
assert.match(html, /painted-river\.webp/);
assert.match(html, /boat\.webp/);
assert.match(html, /data-river-boat-hull/);
assert.match(html, /data-river-boat-farmer/);
assert.doesNotMatch(html, /data-river-character="FARMER"/);
assert.match(html, /Can you help me cross the river\?/);
assert.match(html, /data-river-challenge-load[^>]*>[\s\S]*?OKAY/);
assert.doesNotMatch(html, /challenge-door(?:-|\b)/, "The old challenge trailer must be removed");
assert.match(html, /data-river-music="media\/river-challenge\/puzzle-casual-game-music\.mp3"/);
assert.match(homepage, /import\("\.\/river-challenge\.js"\)/);
assert.match(homepage, /new Audio\(/);
assert.match(challenge, /painted-river\.webp/);
assert.match(challenge, /boat\.webp/);
assert.match(challenge, /data-river-boat-hull/);
assert.match(challenge, /data-river-boat-farmer/);
assert.doesNotMatch(challenge, /data-river-character="FARMER"/);
assert.doesNotMatch(challenge, /CHARACTERS_URL|characters\.webp/);
assert.doesNotMatch(challenge, /renderer\.actors\.get\("FARMER"\)/);
assert.match(challenge, /river-blink-layer/);
assert.doesNotMatch(challenge, /river-eyelids/);
assert.match(challenge, /river-sheep-mouth/);
assert.match(challenge, /# The official names are: sheep, wolf, and hay\./);
assert.match(challenge, /get the hay in the boat\\nrow to the other side/);
assert.match(challenge, /Your plan has no river instructions yet\./);
assert.match(challenge, /Everyone is safe so far, but the crossing is not complete\./);
assert.match(challenge, /data-river-playback-toggle/);
assert.match(challenge, /data-river-speed/);
assert.match(challenge, /data-river-reset/);
assert.match(challenge, /data-river-mute/);
assert.match(challenge, /animation-play-state/);
assert.match(challenge, /renderSemanticTokens/);
assert.match(challenge, /analyzeDynLex/);
assert.match(challenge, /data-river-source-diagnostics/);
assert.match(challenge, /rebaseLspDiagnosticsAfterLines/);
assert.match(challenge, /createRiverChallengeAudio/);
assert.match(challenge, /event\.action === "CROSS" \? "rowing" : "boat"/);
assert.match(challenge, /playTraceEffect\("anxious"/);
assert.match(challenge, /playOneShot\("win"\)/);
assert.doesNotMatch(challenge, /appendHighlightedText|tokenPattern/);
for (const sound of [
  "forest-river-ambience-loop.mp3",
  "boat-whoosh.mp3",
  "sheep-idle.mp3",
  "sheep-anxious.mp3",
  "rowing-paddle.mp3",
  "win-level-up.mp3"
]) {
  assert.match(audio, new RegExp(sound.replace(".", "\\.")));
}
assert.match(audio, /IntersectionObserver/);
assert.match(audio, /visibilitychange/);
assert.match(audio, /playbackRate/);
assert.match(audio, /setIdleEnabled/);
assert.match(audio, /name === "idle" \|\| name === "anxious"/);
assert.match(audio, /is-bellowing/);
assert.ok(
  fs.statSync(paths.ambience).size <= 320_000,
  "The lazily loaded forest-river loop must stay under 320 KB"
);
assert.match(
  attributions,
  /forest-with-small-river-birds-and-nature-field-recording-6735/
);
assert.match(styles, /\[data-river-line-state="active"\]/);
assert.match(styles, /\[data-river-line-state="error"\]/);
assert.match(styles, /\.river-sheep\.is-bellowing \.river-sheep-mouth/);
assert.match(styles, /sheep\.webp/);
assert.match(styles, /sheep-blink\.webp/);
assert.match(styles, /wolf\.webp/);
assert.match(styles, /wolf-blink\.webp/);
assert.match(styles, /hay\.webp/);
assert.match(styles, /boat-blink\.webp/);
assert.doesNotMatch(styles, /characters\.webp/);
assert.match(styles, /\.river-blink-layer/);
assert.doesNotMatch(styles, /river-eyelids/);
assert.match(styles, /\.river-speech::before/);
assert.doesNotMatch(styles, /\.river-speech[^{]*\{[^}]*border-radius:[^;]*5px/s);
assert.match(styles, /\[data-boat-heading="HOME"\]/);
assert.match(styles, /\.river-boat-hull/);
assert.match(styles, /clip-path:/);
assert.doesNotMatch(homepageStyles, /characters\.webp|river-farmer/);
assert.match(styles, /prefers-reduced-motion/);

for (const pattern of [
  "get {river passenger:passenger} in [the|] boat",
  "take {river passenger:passenger} aboard",
  "get {river passenger:passenger} out of [the|] boat",
  "row to the other side",
  "row back",
  "cross the river",
  "row across the water"
]) {
  assert.match(library, new RegExp(pattern.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")));
}
assert.match(library, /function \[the\|\] sheep:/);
assert.match(library, /function \[the\|\] wolf:/);
assert.match(library, /function \[the\|\] hay:/);
assert.doesNotMatch(
  library,
  /^\s+(?:get|put|load|take|unload|let) (?:the )?(?:sheep|wolf|hay)\b/m,
  "River commands must accept a typed passenger instead of hard-coded zero-argument phrases"
);

assert.match(library, /RIVER\|ERROR\|there is no sheep to pick up/);
assert.match(library, /RIVER\|ERROR\|there is no hay to pick up/);
assert.match(library, /RIVER\|DANGER\|WOLF\|SHEEP/);
assert.match(library, /RIVER\|DANGER\|SHEEP\|HAY/);
assert.match(library, /RIVER\|SUCCESS/);

const {
  applyRiverEvent,
  createInitialRiverScene,
  parseRiverTrace
} = await import(pathToFileURL(paths.model).href);

const trace = parseRiverTrace([
  "RIVER|COMMAND|LOAD|HAY",
  "RIVER|ACTION|LOAD|HAY",
  "RIVER|COMMAND|CROSS",
  "RIVER|ACTION|CROSS|FAR",
  "RIVER|DANGER|WOLF|SHEEP",
  "RIVER|ERROR|the wolf ate the sheep",
  ""
].join("\n"));

assert.equal(trace.commands.length, 2);
assert.deepEqual(trace.commands[0], {
  action: "LOAD",
  subject: "HAY",
  events: [{ type: "ACTION", action: "LOAD", subject: "HAY" }]
});
assert.deepEqual(trace.commands[1], {
  action: "CROSS",
  subject: null,
  events: [
    { type: "ACTION", action: "CROSS", subject: "FAR" },
    { type: "DANGER", predator: "WOLF", prey: "SHEEP" },
    { type: "ERROR", message: "the wolf ate the sheep" }
  ]
});
assert.equal(trace.outcome, "failure");
assert.equal(trace.message, "the wolf ate the sheep");

let scene = createInitialRiverScene();
scene = applyRiverEvent(scene, { type: "ACTION", action: "LOAD", subject: "HAY" });
scene = applyRiverEvent(scene, { type: "ACTION", action: "CROSS", subject: "FAR" });
assert.deepEqual(scene, {
  farmer: "FAR",
  boat: "FAR",
  boatHeading: "FAR",
  cargo: "HAY",
  sheep: "HOME",
  wolf: "HOME",
  hay: "BOAT",
  danger: null,
  complete: false
});
scene = applyRiverEvent(scene, { type: "DANGER", predator: "WOLF", prey: "SHEEP" });
assert.deepEqual(scene.danger, { predator: "WOLF", prey: "SHEEP" });

let returnScene = createInitialRiverScene();
returnScene = applyRiverEvent(returnScene, { type: "ACTION", action: "LOAD", subject: "SHEEP" });
returnScene = applyRiverEvent(returnScene, { type: "ACTION", action: "CROSS", subject: "FAR" });
returnScene = applyRiverEvent(returnScene, { type: "ACTION", action: "CROSS", subject: "HOME" });
assert.equal(returnScene.boatHeading, "HOME");

assert.throws(
  () => parseRiverTrace("not a river event"),
  /Invalid river challenge output/
);
assert.throws(
  () => applyRiverEvent(createInitialRiverScene(), { type: "ACTION", action: "DANCE", subject: null }),
  /Unknown river action/
);
assert.throws(
  () => applyRiverEvent(createInitialRiverScene(), {
    type: "DANGER",
    predator: "WOLF",
    prey: "SHEEP"
  }),
  /Inconsistent DANGER/
);

console.log("River challenge structure, protocol, and scene model are valid.");
