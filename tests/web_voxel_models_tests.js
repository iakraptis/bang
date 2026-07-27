"use strict";

const assert = require("node:assert/strict");
const voxels = require("../web/voxel-models.js");

/* The figures on the 3D battlefield are built, not drawn, so what can be
   checked here is that every character in the game gets a figure, that the
   figures are actually distinguishable from one another, and that they are
   the shape the stage expects to place and light. */

const HEX = /^#[0-9a-f]{6}$/i;

/* ---------- every character is represented ---------- */

const keys = Object.keys(voxels.CHARACTERS);
assert.equal(keys.length, 24, "the roster is 24 characters, 12 a side");

for (const key of keys) {
  const figure = voxels.buildFigure(key);
  assert.ok(figure.voxels.length > 150, `${key} is too sparse to read as a figure`);
  assert.ok(figure.voxels.length < 2500, `${key} is too heavy for a stage of four`);
  for (const voxel of figure.voxels) {
    assert.match(voxel.color, HEX, `${key} has a voxel with a bad colour`);
    assert.ok(Number.isInteger(voxel.x) && Number.isInteger(voxel.y) && Number.isInteger(voxel.z),
              `${key} has a voxel off the grid`);
  }
}

/* ---------- no two characters look alike ---------- */
{
  const shapes = new Map();
  for (const key of keys) {
    const signature = voxels.buildFigure(key).voxels
      .map(v => `${v.x},${v.y},${v.z},${v.color}`).sort().join("|");
    assert.ok(!shapes.has(signature),
              `${key} is identical to ${shapes.get(signature)}`);
    shapes.set(signature, key);
  }
}

/* ---------- figures stand on the ground, facing forward ---------- */
for (const key of keys) {
  const { bounds } = voxels.buildFigure(key);
  assert.equal(bounds.min[1], 0, `${key} does not stand on the ground`);
  assert.ok(bounds.size[1] >= 18 && bounds.size[1] <= 30,
            `${key} is ${bounds.size[1]} tall, outside what the camera frames`);
  // The stage views the rows from the side, so a figure has to have depth;
  // a three-deep slab under a nine-wide hat reads as a table on a stick.
  assert.ok(bounds.size[2] >= 5, `${key} is only ${bounds.size[2]} deep`);
  assert.ok(bounds.size[0] >= 5, `${key} is only ${bounds.size[0]} wide`);
  // Stocky, not statuesque: about three heads tall, never seven.
  assert.ok(bounds.size[1] / bounds.size[0] < 4,
            `${key} is too tall for its width to read as a person`);
}

/* ---------- traits actually change the figure ---------- */
{
  // A hat is worn above the head, so it lifts the figure's overall height.
  const bare = voxels.buildFigure("west_harding").bounds.size[1];   // no hat
  const hatted = voxels.buildFigure("wild_bill").bounds.size[1];    // wide brim
  assert.ok(hatted > bare, "a hat should sit above a bare head");

  // A sombrero is the widest thing anyone wears.
  const sombrero = voxels.buildFigure("toco_ramirez").bounds.size[0];
  const wideBrim = voxels.buildFigure("wild_bill").bounds.size[0];
  assert.ok(sombrero > wideBrim, "the sombrero should out-span a stetson");

  // The Daltons ride three abreast and are far wider than anyone else.
  const daltons = voxels.buildFigure("dalton_bros").bounds.size[0];
  assert.ok(daltons > wideBrim * 2, "the Dalton Bros should be three figures wide");

  // Build changes the silhouette.
  const broad = voxels.buildFigure("bull_anderson").bounds.size[0];
  const slim = voxels.buildFigure("west_harding").bounds.size[0];
  assert.ok(broad > slim, "a broad build should be wider than a slim one");
}

/* ---------- a character the roster gains later still gets a figure ---------- */
{
  const unknown = voxels.buildFigure("someone_new");
  assert.ok(unknown.voxels.length > 150, "an unlisted character still stands up");
  assert.equal(unknown.traits, voxels.DEFAULT_CHARACTER);
}

/* ---------- props ---------- */
{
  const cards = Object.keys(voxels.CARD_PROPS);
  assert.ok(cards.length >= 30, "every card that can be played needs a prop");
  for (const key of cards) {
    const prop = voxels.buildProp(key);
    assert.ok(prop.voxels.length > 0, `${key} builds an empty prop`);
    assert.ok(prop.voxels.length < 200, `${key} is too heavy for a thrown prop`);
    for (const voxel of prop.voxels) {
      assert.match(voxel.color, HEX, `${key} has a voxel with a bad colour`);
    }
  }
  // An unlisted card still throws something recognisable onto the field.
  assert.ok(voxels.buildProp("brand_new_card").voxels.length > 0);

  // Cards that are the same object differ only in tint, and cards that are
  // different objects really are different shapes.
  assert.equal(voxels.buildProp("colt").prop, voxels.buildProp("schofield").prop);
  assert.notEqual(voxels.buildProp("colt").tint, voxels.buildProp("schofield").tint);
  assert.notEqual(voxels.buildProp("colt").prop, voxels.buildProp("dynamite").prop);
}

/* ---------- shading ---------- */
{
  assert.equal(voxels.shade("#808080", 16), "#909090");
  assert.equal(voxels.shade("#000000", -40), "#000000", "shading clamps at black");
  assert.equal(voxels.shade("#ffffff", 40), "#ffffff", "shading clamps at white");
  assert.match(voxels.shade("#6d4b33", -22), HEX);
}

/* ---------- bounds ---------- */
{
  const empty = voxels.bounds([]);
  assert.deepEqual(empty.size, [0, 0, 0]);
  const one = voxels.bounds([{ x: 2, y: 3, z: 4, color: "#ffffff" }]);
  assert.deepEqual(one.size, [1, 1, 1]);
  assert.deepEqual(one.min, [2, 3, 4]);
}

console.log("web voxel model tests passed");
