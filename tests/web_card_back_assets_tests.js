"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const root = path.join(__dirname, "..");
const css = fs.readFileSync(path.join(root, "web", "styles.css"), "utf8");
const app = fs.readFileSync(path.join(root, "web", "app.js"), "utf8");

const assets = ["card_back_law.png", "card_back_outlaw.png", "card_back_reserve.png"];
const pngSignature = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

function pngSize(file) {
  const bytes = fs.readFileSync(file);
  assert.deepEqual(bytes.subarray(0, 8), pngSignature, `${file} is not a PNG`);
  assert.equal(bytes.subarray(12, 16).toString("ascii"), "IHDR", `${file} has no PNG header`);
  return [bytes.readUInt32BE(16), bytes.readUInt32BE(20)];
}

for (const file of assets) {
  const assetPath = path.join(root, "web", "cards", file);
  assert.deepEqual(pngSize(assetPath), [250, 389], `${file} must match the card aspect ratio`);
  assert.match(css, new RegExp(`cards/${file.replace(".", "\\.")}`), `${file} is not used`);
}

assert.match(css, /\.mini-back\.law,\s*\.card-back\.law\s*\{[^}]*card_back_law\.png/s);
assert.match(css, /\.mini-back\.outlaw,\s*\.card-back\.outlaw\s*\{[^}]*card_back_outlaw\.png/s);
assert.match(css, /\.reserve-stack \.char-back\s*\{[^}]*card_back_reserve\.png/s);
assert.match(css, /\.char\.facedown\s*\{[^}]*card_back_reserve\.png/s);

// The decorative cards in each pile must sit behind the illustrated top card.
assert.match(css, /\.deck-stack \.mini-back\s*\{[^}]*z-index:\s*1/s);
assert.match(css, /\.deck-stack::before,\s*\.deck-stack::after\s*\{[^}]*z-index:\s*0/s);
assert.match(css, /\.reserve-stack \.char-back\s*\{[^}]*z-index:\s*1/s);
assert.match(css, /\.reserve-stack::before,\s*\.reserve-stack::after\s*\{[^}]*z-index:\s*0/s);

// Face-down setup seats use artwork rather than the old question-mark placeholder.
assert.doesNotMatch(app, /fallen \? "☠" : "\?"/);

console.log("web card back asset tests passed");
