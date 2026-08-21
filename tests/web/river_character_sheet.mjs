import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";

const projectDir = path.resolve(import.meta.dirname, "../..");
const webDir = path.join(projectDir, "web");
const files = {
  html: path.join(webDir, "river-character-sheet.html"),
  script: path.join(webDir, "river-character-sheet.js"),
  styles: path.join(webDir, "river-character-sheet.css"),
  art: path.join(webDir, "river-character-art.js")
};

for (const [name, filePath] of Object.entries(files)) {
  assert.ok(fs.existsSync(filePath), `Missing river character sheet ${name}`);
}

const html = fs.readFileSync(files.html, "utf8");
const script = fs.readFileSync(files.script, "utf8");
const styles = fs.readFileSync(files.styles, "utf8");
const art = fs.readFileSync(files.art, "utf8");

assert.match(html, /river-challenge\.css/);
assert.match(html, /river-character-sheet\.css/);
assert.match(html, /river-character-sheet\.js/);
assert.match(html, /data-river-character-sheet/);
assert.match(html, /data-river-sheet-playback/);
assert.match(html, /data-river-sheet-scrubber/);
assert.match(html, /data-river-sheet-boxes/);

for (const character of ["BOAT", "SHEEP", "WOLF", "HAY"]) {
  assert.match(script, new RegExp(`character: "${character}"`));
}
for (const state of [
  "idle",
  "blink",
  "left-eyelid",
  "right-eyelid",
  "home-facing",
  "bellowing",
  "scuffle",
  "eaten",
  "licking"
]) {
  assert.match(script, new RegExp(`state: "${state}"`));
}
assert.match(script, /river-boat-farmer-blink/);
assert.match(script, /river-sheep-blink/);
assert.match(script, /river-sheep-mouth/);
assert.match(script, /river-wolf-blink/);
assert.match(script, /createWolfTongue/);
assert.match(script, /createWolfTongueLickSpecifications/);
assert.match(art, /river-wolf-tongue-shape/);
assert.doesNotMatch(art, /river-wolf-tongue-(?:thick|thin)/);
assert.match(art, /WOLF_TONGUE_LICK_DURATION/);
assert.match(script, /river-wolf-mouth-line/);
assert.match(script, /viewBox/);
assert.match(script, /getAnimations\(\{ subtree: true \}\)/);
assert.match(script, /animation\.currentTime/);

assert.match(styles, /\.river-sheet-frame-blink/);
assert.match(styles, /\.river-sheet-isolated-eyelid/);
assert.match(styles, /data-overlay-boxes="true"/);
assert.match(styles, /--river-animation-play-state: running/);

for (const filePath of Object.values(files)) {
  const lineCount = fs.readFileSync(filePath, "utf8").split("\n").length;
  assert.ok(lineCount < 1000, `${path.relative(projectDir, filePath)} must stay under 1000 lines`);
}

console.log("River character animation inspection sheet is complete.");
