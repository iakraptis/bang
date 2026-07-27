"use strict";

const assert = require("node:assert/strict");
const fx = require("../web/card-fx.js");

/* ---------- motifs ---------- */

// Every listed key must resolve to a motif that actually exists.
for (const [key, motif] of Object.entries(fx.MOTIF_BY_KEY)) {
  assert.ok(fx.MOTIFS[motif], `${key} maps to unknown motif ${motif}`);
}

// Guns recoil, Dynamite shatters, the Rattlesnake slithers.
assert.equal(fx.motifFor({ key: "colt", kind: "bang" }, "play").exit, "recoil");
assert.equal(fx.motifFor({ key: "dynamite", kind: "equipment" }, "play").exit, "shatter");
assert.equal(fx.motifFor({ key: "rattlesnake", kind: "equipment" }, "play").exit, "slither");
assert.equal(fx.motifFor({ key: "beer", kind: "action" }, "play").exit, "settle");
assert.equal(fx.motifFor({ key: "general_store", kind: "action" }, "play").exit, "pinwheel");

// No two motifs share an exit path — each card kind really does leave
// the table its own way.
const exits = Object.values(fx.MOTIFS).map(motif => motif.exit);
assert.equal(new Set(exits).size, exits.length, "motif exit paths must be distinct");

// An unlisted card still animates, by kind.
assert.equal(fx.motifNameFor({ key: "brand_new_gun", kind: "bang" }, "play"), "gunshot");
assert.equal(fx.motifNameFor({ key: "brand_new_gear", kind: "equipment" }, "play"), "rig");
assert.equal(fx.motifNameFor({ key: "mystery", kind: null }, "play"), "deal");

// A card spent to answer a hit always reads as a block, whatever it
// normally does: a Barrel equips (rig) but blocks (deflect).
assert.equal(fx.motifNameFor({ key: "barrel", kind: "equipment" }, "play"), "rig");
assert.equal(fx.motifNameFor({ key: "barrel", kind: "equipment" }, "avoid"), "deflect");
assert.equal(fx.motifNameFor({ key: "missed", kind: "action" }, "avoid"), "deflect");

/* ---------- target zones ---------- */
// Zone names are relative to the human, engine targets are relative to the
// actor, so the AI's "opponent_active" is the human's own active character.
assert.equal(fx.targetZone("opponent_active", 0, 0), "opponent_active");
assert.equal(fx.targetZone("opponent_active", 1, 0), "own_active");
assert.equal(fx.targetZone("own_rearguard", 1, 0), "opponent_rearguard");
assert.equal(fx.targetZone("own_rearguard", 0, 0), "own_rearguard");
assert.equal(fx.targetZone("", 0, 0), null);
assert.equal(fx.targetZone(undefined, 0, 0), null);

/* ---------- reading plays out of a state diff ---------- */

function state({ log = [], discard = [], equipment = [[], []] } = {}) {
  return {
    sessionId: "s",
    humanPlayer: 0,
    log,
    discard,
    players: [0, 1].map(index => ({
      positions: [
        { role: "active", equipment: equipment[index] || [] },
        { role: "rearguard", equipment: [] },
      ],
    })),
  };
}

const colt = { instance: 11, key: "colt", kind: "bang", name: "Colt" };
const missed = { instance: 12, key: "missed", kind: "action", name: "Missed!" };
const dynamite = { instance: 13, key: "dynamite", kind: "equipment", name: "Dynamite" };

{
  // The opponent fires, the human blocks: two ceremonies, in log order, each
  // paired with the card instance that reached the discard pile.
  const prev = state({ log: [{ player: 0, engineText: "end_play" }] });
  const next = state({
    log: [
      { player: 0, engineText: "end_play" },
      { player: 1, engineText: "play(colt,opponent_active)" },
      { player: 0, engineText: "avoid(missed)" },
    ],
    discard: [colt, missed],
  });
  const events = fx.playEvents(prev, next);
  assert.equal(events.length, 2);

  assert.equal(events[0].verb, "play");
  assert.equal(events[0].instance, 11);
  assert.equal(events[0].destination, "discard");
  assert.equal(events[0].isHuman, false);
  assert.equal(events[0].zone, "own_active", "the AI shot at the human's active");
  assert.equal(events[0].motif.exit, "recoil");

  assert.equal(events[1].verb, "avoid");
  assert.equal(events[1].instance, 12);
  assert.equal(events[1].isHuman, true);
  assert.equal(events[1].zone, "own_active", "a block guards the side that played it");
  assert.equal(events[1].motif.exit, "ricochet");
}

{
  // Equipment does not reach the discard pile — it is paired with the
  // character row it landed on instead.
  const prev = state();
  const next = state({
    log: [{ player: 1, engineText: "play(dynamite,opponent_rearguard)" }],
    equipment: [[dynamite], []],
  });
  const [event] = fx.playEvents(prev, next);
  assert.equal(event.destination, "equipment");
  assert.equal(event.instance, 13);
  assert.equal(event.zone, "own_rearguard");
  assert.equal(event.motif.exit, "shatter");
}

{
  // Bookkeeping actions never get a ceremony.
  const prev = state();
  const next = state({
    log: [
      { player: 0, engineText: "end_play" },
      { player: 0, engineText: "discard(hat)" },
      { player: 0, engineText: "reload(bank)" },
      { player: 0, engineText: "use_ability(wild_bill)" },
    ],
  });
  assert.deepEqual(fx.playEvents(prev, next), []);
}

{
  // A card the player never saw still animates, without an instance.
  const prev = state();
  const next = state({ log: [{ player: 1, engineText: "play(gatling,opponent_active)" }] });
  const [event] = fx.playEvents(prev, next);
  assert.equal(event.instance, null);
  assert.equal(event.key, "gatling");
  assert.equal(event.motif.exit, "scatter");
}

{
  // A trimmed log (the server caps its length) must not replay the world.
  const prev = state({ log: [{ engineText: "a" }, { engineText: "b" }, { engineText: "c" }] });
  const next = state({ log: [{ player: 0, engineText: "play(colt,opponent_active)" }] });
  assert.deepEqual(fx.playEvents(prev, next), []);
}

/* ---------- timing plan ---------- */
{
  const prev = state();
  const next = state({
    log: [
      { player: 1, engineText: "play(colt,opponent_active)" },
      { player: 0, engineText: "avoid(missed)" },
    ],
    discard: [colt, missed],
  });

  const plans = fx.planPlays(prev, next, { startAt: 700 });
  assert.equal(plans[0].enterAt, 700, "ceremonies queue behind pending reveals");
  assert.equal(plans[0].impactAt, 700 + fx.TIMING.enter);
  assert.ok(plans[0].endsAt > plans[0].exitAt);
  assert.ok(plans[1].enterAt >= plans[0].endsAt, "ceremonies never overlap");
  // You know what you just played; the opponent's card is held longer.
  assert.ok(plans[1].holdMs < plans[0].holdMs);

  // Reduced motion keeps the cues but drops the choreography.
  const flat = fx.planPlays(prev, next, { reducedMotion: true });
  assert.equal(flat[0].enterAt, 0);
  assert.equal(flat[0].holdMs, 0);
  assert.equal(flat[0].impactAt, 0);
  assert.ok(flat[1].impactAt > flat[0].impactAt, "cues stay staggered");
}

/* ---------- seats: who fell, who took their place ---------- */

// A table with an explicit character (or none) in each seat.
// seats[player] = [activeCharacter, rearguardCharacter]
function table(seats, { setupComplete = true } = {}) {
  return {
    sessionId: "s",
    humanPlayer: 0,
    setupComplete,
    log: [],
    discard: [],
    players: [0, 1].map(index => ({
      side: index === 0 ? "law" : "outlaw",
      positions: ["active", "rearguard"].map((role, slot) => ({
        role,
        equipment: [],
        character: seats[index][slot]
          ? { id: seats[index][slot].id, key: seats[index][slot].key, name: seats[index][slot].name }
          : null,
        life: seats[index][slot] ? seats[index][slot].life : null,
      })),
    })),
  };
}

const sheriff = { id: 1, key: "sheriff", name: "Sheriff", life: 4 };
const deputy = { id: 2, key: "deputy", name: "Deputy", life: 3 };
const bandit = { id: 3, key: "bandit", name: "Bandit", life: 4 };
const renegade = { id: 4, key: "renegade", name: "Renegade", life: 4 };
const fresh = { id: 9, key: "gunslinger", name: "Gunslinger", life: 4 };

{
  // No prior state (a restored session, a brand new table): nothing to stage.
  assert.deepEqual(fx.characterEvents(null, table([[sheriff, deputy], [bandit, renegade]])),
                   { fallen: [], arrivals: [] });
}

{
  // Shot down and replaced in one engine step: one fall, one arrival.
  const prev = table([[sheriff, deputy], [bandit, renegade]]);
  const next = table([[sheriff, deputy], [{ ...fresh }, renegade]]);
  const { fallen, arrivals } = fx.characterEvents(prev, next);
  assert.equal(fallen.length, 1);
  assert.equal(fallen[0].id, bandit.id);
  assert.equal(fallen[0].key, "bandit");
  assert.equal(fallen[0].swept, false, "a character alive a moment ago topples");
  assert.equal(fallen[0].own, false);
  assert.equal(fallen[0].zone, "opponent_active");
  assert.equal(arrivals.length, 1);
  assert.equal(arrivals[0].id, fresh.id);
  assert.equal(arrivals[0].name, "Gunslinger");
  assert.equal(arrivals[0].replacement, true);
  assert.equal(arrivals[0].zone, "opponent_active");
}

{
  // A deferred elimination shows the character at zero life first; it already
  // toppled, so being cleared off the table is only a sweep.
  const down = { ...bandit, life: 0 };
  const prev = table([[sheriff, deputy], [down, renegade]]);
  const next = table([[sheriff, deputy], [null, renegade]]);
  const { fallen } = fx.characterEvents(prev, next);
  assert.equal(fallen.length, 1);
  assert.equal(fallen[0].swept, true);
}

{
  // The last character on a side falls with an empty reserve: no arrival.
  const prev = table([[sheriff, deputy], [bandit, null]]);
  const next = table([[sheriff, deputy], [null, null]]);
  const { fallen, arrivals } = fx.characterEvents(prev, next);
  assert.equal(fallen.length, 1);
  assert.deepEqual(arrivals, []);
}

{
  // Opening reveal: the opponent's seats turn face up once both sides commit.
  // That is an arrival, but not a replacement — nothing rides in from a pile.
  const prev = table([[sheriff, deputy], [null, null]], { setupComplete: false });
  const next = table([[sheriff, deputy], [bandit, renegade]]);
  const { fallen, arrivals } = fx.characterEvents(prev, next);
  assert.deepEqual(fallen, []);
  assert.equal(arrivals.length, 2);
  assert.ok(arrivals.every(who => who.replacement === false));
  assert.ok(arrivals.every(who => who.own === false));
}

{
  // A quiet turn moves nobody.
  const quiet = table([[sheriff, deputy], [bandit, renegade]]);
  assert.deepEqual(fx.characterEvents(quiet, table([[sheriff, deputy], [bandit, renegade]])),
                   { fallen: [], arrivals: [] });
  // Your own side reads as yours, and life rides along for the fall.
  const seated = fx.seatedCharacters(quiet);
  assert.equal(seated.get(sheriff.id).own, true);
  assert.equal(seated.get(sheriff.id).zone, "own_active");
  assert.equal(seated.get(sheriff.id).side, "law");
  assert.equal(seated.get(deputy.id).zone, "own_rearguard");
  assert.equal(seated.get(bandit.id).life, 4);
}

/* ---------- captions ---------- */
assert.equal(fx.playCaption({ verb: "play", isHuman: true }, "You"), "You play");
assert.equal(fx.playCaption({ verb: "play", isHuman: false }, "Outlaw"), "Outlaw plays");
assert.equal(fx.playCaption({ verb: "avoid", isHuman: false }, "Outlaw"), "Outlaw blocks with");

console.log("web card fx tests passed");
