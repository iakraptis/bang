"use strict";

const assert = require("node:assert/strict");
const { effectiveDropZone } = require("../web/targeting.js");

function gameWithPositions(ownPositions, opponentPositions = ownPositions) {
  return {
    humanPlayer: 0,
    players: [
      { positions: ownPositions },
      { positions: opponentPositions },
    ],
  };
}

const livingPair = [
  { role: "active", character: { id: 1 }, life: 3 },
  { role: "rearguard", character: { id: 2 }, life: 2 },
];
const loneSurvivor = [
  { role: "active", character: { id: 1 }, life: 3 },
  { role: "rearguard", character: null, life: null },
];
const pendingElimination = [
  { role: "active", character: { id: 1 }, life: 3 },
  { role: "rearguard", character: { id: 2 }, life: 0 },
];

assert.equal(effectiveDropZone("own_active", gameWithPositions(loneSurvivor)), "own_active");
assert.equal(effectiveDropZone("own_rearguard", gameWithPositions(livingPair)), "own_rearguard");
assert.equal(effectiveDropZone("own_rearguard", gameWithPositions(loneSurvivor)), "own_active");
assert.equal(effectiveDropZone("own_rearguard", gameWithPositions(pendingElimination)), "own_active");
assert.equal(
  effectiveDropZone("opponent_rearguard", gameWithPositions(livingPair, loneSurvivor)),
  "opponent_active",
);
assert.equal(
  effectiveDropZone("own_rearguard", {
    humanPlayer: 1,
    players: [{ positions: livingPair }, { positions: loneSurvivor }],
  }),
  "own_active",
);
assert.equal(effectiveDropZone(undefined, gameWithPositions(loneSurvivor)), undefined);

console.log("web targeting tests passed");
