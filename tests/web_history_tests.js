"use strict";

const assert = require("node:assert/strict");
const history = require("../web/history.js");

const entry = (player, engineText, text) => ({
  player,
  side: player === null ? "system" : player === 0 ? "law" : "outlaw",
  engineText: engineText || "",
  text: text || "",
});

/* ---------- engine-call parsing ---------- */

assert.deepEqual(history.parseEngine("play(colt,opponent_active)"),
  { verb: "play", args: ["colt", "opponent_active"] });
assert.deepEqual(history.parseEngine("avoid(missed)"), { verb: "avoid", args: ["missed"] });
assert.deepEqual(history.parseEngine("end_play"), { verb: "end_play", args: [] });
assert.equal(history.parseEngine(""), null);
assert.equal(history.parseEngine("New duel."), null);

/* ---------- names ---------- */

assert.equal(history.cardName("general_store"), "General Store");
assert.equal(history.cardName("missed"), "Missed!");
assert.equal(history.cardName("indians"), "Indians!");
assert.equal(history.cardName("colt"), "Colt");
assert.equal(history.actorOf("Soundance Kid: Play Colt"), "Soundance Kid");
assert.equal(history.actorOf("no actor here"), "");

/* ---------- a BANG! answered by a Missed! folds into one moment ---------- */

{
  const moments = history.buildHistory([
    entry(1, "play(colt,opponent_active)", "Soundance Kid: Play Colt"),
    entry(0, "avoid(missed)", "The Stranger: Respond with Missed!"),
  ]);
  assert.equal(moments.length, 1);
  assert.equal(moments[0].type, "play");
  assert.equal(moments[0].key, "colt");
  assert.equal(moments[0].actor, "Soundance Kid");
  assert.equal(moments[0].responses.length, 1);
  assert.deepEqual(
    [moments[0].responses[0].kind, moments[0].responses[0].role, moments[0].responses[0].key],
    ["card", "avoid", "missed"]);
}

/* ---------- hits taken and deaths ride the play that caused them ---------- */

{
  const moments = history.buildHistory([
    entry(1, "play(gatling,own_active)", "Soundance Kid: Play Gatling"),
    entry(0, "take_hit", "The Stranger: Take the hit"),
    entry(0, "death(the_stranger)", "The Stranger died."),
    entry(1, "avoid(return_fire)", "Bull Anderson: Respond with Return Fire"),
  ]);
  assert.equal(moments.length, 1);
  const kinds = moments[0].responses.map(response => response.kind);
  assert.deepEqual(kinds, ["hit", "death", "card"]);
  assert.equal(moments[0].responses[1].key, "the_stranger");
}

/* ---------- a General Store carries every draft pick ---------- */

{
  const moments = history.buildHistory([
    entry(0, "play(general_store,own_active)", "Alan Pinkertoon: Play General Store"),
    entry(0, "select(carbine)", "Alan Pinkertoon: Choose Carbine"),
    entry(1, "select(missed)", "Soundance Kid: Choose Missed!"),
    entry(0, "select(scope)", "Alan Pinkertoon: Choose Scope"),
    entry(1, "select(missed)", "Soundance Kid: Choose Missed!"),
  ]);
  assert.equal(moments.length, 1);
  assert.equal(moments[0].responses.length, 4);
  assert.ok(moments[0].responses.every(response => response.role === "pick"));
  // alternating pickers survive the fold — the view colours them by player
  assert.deepEqual(moments[0].responses.map(response => response.player), [0, 1, 0, 1]);
}

/* ---------- a Duel: traded hit cards, and conceding costs a life ---------- */

{
  const moments = history.buildHistory([
    entry(0, "play(duel,opponent_active)", "Alan Pinkertoon: Play Duel"),
    entry(1, "discard(colt)", "Soundance Kid: Discard Colt"),
    entry(0, "discard(knife)", "Alan Pinkertoon: Discard Knife"),
    entry(1, "skip_optional", "Soundance Kid: Concede the Duel"),
  ]);
  assert.equal(moments.length, 1);
  const responses = moments[0].responses;
  assert.deepEqual(responses.map(response => response.kind), ["card", "card", "hit"]);
  assert.ok(responses[2].concede);
}

// Outside a Duel, declining an optional effect is silent.
{
  const moments = history.buildHistory([
    entry(0, "play(schoolmarm,own_active)", "Alan Pinkertoon: Play Schoolmarm"),
    entry(0, "skip_optional", "Alan Pinkertoon: Skip the effect"),
  ]);
  assert.equal(moments[0].responses.length, 0);
}

/* ---------- Stealing: a hidden card leaves as its card back ---------- */

{
  const moments = history.buildHistory([
    entry(1, "play(stealing,opponent_active)", "Bull Anderson: Play Stealing"),
    entry(1, "choose_back(law)", "Bull Anderson: Choose a Law card back"),
  ]);
  assert.deepEqual(
    [moments[0].responses[0].kind, moments[0].responses[0].origin], ["back", "law"]);
}

/* ---------- turn end gathers the hand-limit discards ---------- */

{
  const moments = history.buildHistory([
    entry(0, "play(beer,own_active)", "Alan Pinkertoon: Play Beer"),
    entry(0, "end_play", "Alan Pinkertoon: End turn"),
    entry(0, "discard(missed)", "Alan Pinkertoon: Discard Missed!"),
    entry(0, "discard(barrel)", "Alan Pinkertoon: Discard Barrel"),
    entry(1, "death(slim_poet)", "Slim Poet died."),
    entry(1, "play(knife,opponent_active)", "Soundance Kid: Play Knife"),
  ]);
  assert.deepEqual(moments.map(moment => moment.type), ["play", "turn_end", "play"]);
  const gathered = moments[1].responses;
  // the hand-limit discards, and a start-of-turn reveal death, sit on the break
  assert.deepEqual(gathered.map(response => response.kind), ["card", "card", "death"]);
  assert.ok(gathered.slice(0, 2).every(response => response.role === "hand_limit"));
  assert.equal(moments[2].responses.length, 0);
}

/* ---------- abilities, reloads, swaps, setup, system lines ---------- */

{
  const moments = history.buildHistory([
    entry(null, "", "New duel. You command the Law side."),
    entry(0, "choose_active(left)", "Chooses an active character face down"),
    entry(1, "use_ability(soundance_kid)", "Soundance Kid: Use Soundance Kid"),
    entry(1, "discard(duel)", "Soundance Kid: Discard Duel"),
    entry(1, "discard(wagon)", "Soundance Kid: Discard Wagon"),
    entry(1, "swap", "Soundance Kid: Swap active and rearguard"),
    entry(1, "reload(scope)", "Soundance Kid: Reload Scope"),
  ]);
  assert.deepEqual(moments.map(moment => moment.type),
    ["system", "setup", "ability", "swap", "reload"]);
  assert.equal(moments[2].key, "soundance_kid");
  assert.equal(moments[2].responses.length, 2);
  // the swap closed the ability moment; the reload starts fresh
  assert.equal(moments[4].responses.length, 0);
}

/* ---------- a trimmed log: orphan responses fold into a prior moment ---------- */

{
  const moments = history.buildHistory([
    entry(0, "avoid(missed)", "The Stranger: Respond with Missed!"),
    entry(0, "take_hit", "The Stranger: Take the hit"),
    entry(1, "play(colt,opponent_active)", "Soundance Kid: Play Colt"),
  ]);
  assert.deepEqual(moments.map(moment => moment.type), ["prior", "play"]);
  assert.equal(moments[0].responses.length, 2);
}

/* ---------- play_as_colt keeps its own marking ---------- */

{
  const moments = history.buildHistory([
    entry(0, "play_as_colt(missed,opponent_active)", "Annie Oakey: Play Missed! as Colt"),
  ]);
  assert.ok(moments[0].asColt);
  assert.equal(moments[0].key, "missed");
}

/* ---------- silent decisions stay silent ---------- */

{
  const moments = history.buildHistory([
    entry(0, "play(remington,opponent_active)", "The Stranger: Play Remington"),
    entry(1, "avoid(missed)", "Bull Anderson: Respond with Missed!"),
    entry(0, "accept_optional", "The Stranger: Use the effect"),
    entry(0, "choose_reveal(first)", "Bart Masterson: Choose first reveal"),
    entry(0, "choose_swap(own)", "The Stranger: Swap your characters"),
  ]);
  assert.equal(moments.length, 1);
  assert.equal(moments[0].responses.length, 1);
}

console.log("web history tests passed");
