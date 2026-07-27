"use strict";

/* The battlefield itself cannot be checked without a browser — there is no
   WebGL here and nothing to look at. What can be checked is the promise the
   stage makes to the deck: that every card which can reach the table has a
   scene of its own, that the objects those scenes reach for actually build,
   and that the rig the figures are aimed with lines up with the figures.

   The module is an ES module that expects a page, so the two things it looks
   for on the global object are supplied first: the voxel models it builds
   everything out of, and an event target to announce itself on. */

const assert = require("node:assert/strict");
const path = require("node:path");
const { pathToFileURL } = require("node:url");

const models = require("../web/voxel-models.js");
const fx = require("../web/card-fx.js");

globalThis.BangVoxelModels = models;
if (typeof globalThis.dispatchEvent !== "function") {
  const bus = new EventTarget();
  globalThis.dispatchEvent = bus.dispatchEvent.bind(bus);
  globalThis.addEventListener = bus.addEventListener.bind(bus);
}

const stageUrl = pathToFileURL(path.join(__dirname, "..", "web", "voxel-stage.js"));

async function main() {
  const stage = (await import(stageUrl.href)).default;

  /* ---------- every card is staged ---------- */
  const { plays, blocks } = stage.staged();
  const deck = Object.keys(fx.MOTIF_BY_KEY);
  assert.ok(deck.length >= 31, "the deck is 31 cards");
  for (const key of deck) {
    assert.ok(plays.includes(key), `${key} has no 3D scene of its own`);
  }
  // A card spent to survive is a different scene, and the ones that can be
  // spent that way each have theirs.
  for (const key of ["missed", "return_fire", "barrel", "hat", "default"]) {
    assert.ok(blocks.includes(key), `${key} has no block scene`);
  }

  /* ---------- the objects those scenes reach for ---------- */

  // Every card that puts a gun in somebody's hand has one built for it, the
  // muzzle is past the end of the barrel, and the grip is where the hand is.
  const armed = ["colt", "schofield", "remington", "peacemaker", "derringer",
                 "winchester", "carbine", "shotgun", "gatling", "fanning",
                 "knife", "indians", "duel"];
  for (const key of armed) {
    const weapon = models.buildHeldWeapon(key);
    assert.ok(weapon, `${key} has no weapon to hold`);
    assert.ok(weapon.voxels.length > 8, `${key} is too sparse to read as a weapon`);
    assert.ok(weapon.voxels.length < 120, `${key} is too heavy to hold`);
    assert.ok(weapon.muzzle[0] > weapon.bounds.max[0] - 2,
              `${key} fires from inside its own barrel`);
    assert.ok(weapon.bounds.min[0] <= 0, `${key} has no grip behind the action`);
  }
  // A rifle really is longer than a revolver, and a hold-out pistol is shorter.
  const reach = key => models.buildHeldWeapon(key).muzzle[0];
  assert.ok(reach("winchester") > reach("colt") * 1.5, "a rifle should out-reach a Colt");
  assert.ok(reach("derringer") < reach("colt"), "a Derringer is a palmed gun");
  assert.ok(reach("peacemaker") > reach("colt"), "the Peacemaker has the long barrel");
  // A card with nothing to hold says so rather than handing back an empty gun.
  assert.equal(models.buildHeldWeapon("beer"), null);

  /* ---------- blue cards that stay on the character ---------- */
  const MOUNTS = new Set(["ground", "crown", "head", "torso", "back", "belt", "side"]);
  const equipment = ["barrel", "hat", "scope", "gun_belt", "bank", "clock",
                     "dynamite", "rattlesnake", "stampede"];
  for (const key of equipment) {
    const gear = models.buildAttachment(key);
    assert.ok(gear, `${key} stays in play but has nothing on the field`);
    assert.ok(MOUNTS.has(gear.mount), `${key} hangs off a mount the stage does not know`);
    assert.equal(gear.offset.length, 3, `${key} has no place to sit`);
    assert.ok(gear.voxels.length > 5, `${key} is too sparse to see`);
    assert.ok(gear.voxels.length < 400, `${key} is too heavy to leave standing`);
    if (gear.origin === "hung") {
      assert.ok(gear.bounds.min[1] < 0, `${key} hangs from its mount but sits above it`);
    } else {
      assert.ok(gear.bounds.min[1] >= 0, `${key} stands on its mount but is modelled below it`);
    }
  }
  // An action card is not gear and never stays behind.
  assert.equal(models.buildAttachment("beer"), null);

  /* ---------- the rig the arm swings on ---------- */
  for (const key of Object.keys(models.CHARACTERS)) {
    const figure = models.buildFigure(key);
    // Taking the gun arm off must not take anything else with it.
    assert.ok(figure.body.length < figure.voxels.length,
              `${key} has no separate gun arm`);
    assert.ok(figure.voxels.length - figure.body.length <= 40,
              `${key} loses more than an arm when the arm comes off`);
    assert.ok(figure.arm.voxels.length > 6, `${key} has an arm too thin to see`);
    // The shoulder is up at shoulder height and out to one side; the hand
    // hangs below it, which is the whole reason the hinge works.
    assert.equal(figure.arm.pivot[1], models.HEIGHTS.shoulder);
    assert.ok(Math.abs(figure.arm.pivot[0]) >= 3, `${key} has its arm inside its chest`);
    assert.ok(figure.arm.hand[1] < 0, `${key} holds its gun above its own shoulder`);
    assert.equal(figure.arm.length, models.HEIGHTS.shoulder - models.HEIGHTS.belt);
  }
  // Gunhands carry something at rest; a man with a rifle across his chest or
  // a hand of cards does not also palm a revolver.
  assert.equal(models.buildFigure("wyatt_ear").idleWeapon, "colt");     // revolver
  assert.equal(models.buildFigure("wild_bill").idleWeapon, "colt");     // twin
  assert.equal(models.buildFigure("buffalo_bell").idleWeapon, null);    // rifle
  assert.equal(models.buildFigure("bart_masterson").idleWeapon, null);  // cards

  console.log("web voxel stage tests passed");
}

main().catch(error => {
  console.error(error);
  process.exit(1);
});
