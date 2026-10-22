(function (root, factory) {
  "use strict";

  const api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    root.BangCardFx = api;
  }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  /* ------------------------------------------------------------------
     Card motifs.

     Every card takes the stage and leaves for the discard pile in its own
     way: a Colt kicks back with the recoil, Dynamite bursts into shards,
     a Rattlesnake slithers off, Beer settles like foam. A motif bundles the
     stage treatment (hold time, burst, sound) with the exit path.

     Card keys match the engine's table in src/static_data.cpp; `kind`
     ("bang" / "equipment" / "action") covers anything unlisted, so a new
     card animates sensibly before it is given a motif of its own.
     ------------------------------------------------------------------ */

  const MOTIFS = {
    // guns that fire a single, avoidable shot
    gunshot: { exit: "recoil", tone: "danger", sound: "shot", burst: "muzzle", hold: 640, shake: false },
    // sprays: everything that hits more than once or more than one character
    barrage: { exit: "scatter", tone: "danger", sound: "shot", burst: "spray", hold: 720, shake: true },
    // the Duel: two hands trading cards until one blinks
    standoff: { exit: "clash", tone: "danger", sound: "clash", burst: "clash", hold: 760, shake: true },
    // Dynamite: fuse, flash, splinters
    blast: { exit: "shatter", tone: "danger", sound: "boom", burst: "shards", hold: 740, shake: true },
    // Rattlesnake: coils and slides away
    serpent: { exit: "slither", tone: "venom", sound: "rattle", burst: "venom", hold: 700, shake: false },
    // Beer / Strong Ale: warmth, foam, relief
    tonic: { exit: "settle", tone: "heal", sound: "heal", burst: "bubbles", hold: 640, shake: false },
    // anything spent to answer a hit
    deflect: { exit: "ricochet", tone: "guard", sound: "missed", burst: "shield", hold: 620, shake: false },
    // Stealing / Schoolmarm: a hand reaches across the table
    plunder: { exit: "swipe", tone: "greed", sound: "swipe", burst: "grab", hold: 680, shake: false },
    // General Store / Wagon: cards spread out on the felt
    bazaar: { exit: "pinwheel", tone: "neutral", sound: "draw", burst: "fan", hold: 660, shake: false },
    // Water / Stampede: characters trade places
    shuffle: { exit: "tumble", tone: "neutral", sound: "whoosh", burst: "swap", hold: 660, shake: false },
    // gear that bolts onto a character and stays there
    rig: { exit: "slot", tone: "gear", sound: "snap", burst: "bolt", hold: 580, shake: false },
    // plain fallback
    deal: { exit: "toss", tone: "neutral", sound: "whoosh", burst: "none", hold: 600, shake: false },
  };

  const MOTIF_BY_KEY = {
    colt: "gunshot", schofield: "gunshot", remington: "gunshot",
    peacemaker: "gunshot", winchester: "gunshot", carbine: "gunshot",
    derringer: "gunshot", knife: "gunshot",

    gatling: "barrage", fanning: "barrage", indians: "barrage", shotgun: "barrage",

    duel: "standoff",
    dynamite: "blast",
    rattlesnake: "serpent",

    beer: "tonic", strong_ale: "tonic",

    missed: "deflect", return_fire: "deflect",

    stealing: "plunder", schoolmarm: "plunder",
    general_store: "bazaar", wagon: "bazaar",
    water: "shuffle", stampede: "shuffle",

    bank: "rig", clock: "rig", gun_belt: "rig", scope: "rig",
    hat: "rig", barrel: "rig",
  };

  const MOTIF_BY_KIND = { bang: "gunshot", equipment: "rig", action: "deal" };

  // A card spent to answer a hit always reads as a block, whatever it does on
  // its own turn — a Barrel torn off a character, a Hat thrown up, a Missed!.
  function motifNameFor(card, verb) {
    if (verb === "avoid") return "deflect";
    return MOTIF_BY_KEY[card?.key] || MOTIF_BY_KIND[card?.kind] || "deal";
  }

  function motifFor(card, verb) {
    const id = motifNameFor(card, verb);
    return { id, ...MOTIFS[id] };
  }

  /* ------------------------------------------------------------------
     Reading plays out of the state diff.

     The server logs every resolved action with an engine string
     ("play(colt,opponent_active)", "avoid(missed)", …) but no card instance,
     while the discard pile and the equipment rows carry instances but no
     verb. Pairing the two by key, in log order, gives each play a card to
     animate and a place to animate it to.
     ------------------------------------------------------------------ */

  const ENGINE_CALL = /^([a-z_]+)\(([^,)]*)(?:,([^)]*))?\)$/;
  const CEREMONY_VERBS = new Set(["play", "play_as_colt", "avoid", "use_resource"]);

  // The server trims its log to the most recent entries, so once it is full
  // two states have the same length however much happened between them.
  // Entries carry a session-wide sequence number for exactly that reason;
  // the length comparison is only a fallback for servers that predate it.
  function newLogEntries(prev, next) {
    const before = prev?.log || [];
    const after = next?.log || [];
    const sequenced = entries => entries.every(entry => Number.isFinite(entry?.sequence));
    if (before.length && sequenced(before) && sequenced(after)) {
      const last = Math.max(...before.map(entry => entry.sequence));
      return after.filter(entry => entry.sequence > last);
    }
    return before.length <= after.length ? after.slice(before.length) : [];
  }

  function newDiscards(prev, next) {
    const before = prev?.discard?.length ?? 0;
    const after = next?.discard || [];
    return after.length >= before ? after.slice(before) : [];
  }

  function equipmentInstances(state) {
    const seen = new Set();
    for (const player of state?.players || []) {
      for (const position of player.positions || []) {
        for (const card of position.equipment || []) seen.add(card.instance);
      }
    }
    return seen;
  }

  function newEquipment(prev, next) {
    const before = equipmentInstances(prev);
    const arrivals = [];
    (next?.players || []).forEach((player, index) => {
      for (const position of player.positions || []) {
        for (const card of position.equipment || []) {
          if (!before.has(card.instance)) arrivals.push({ card, player: index });
        }
      }
    });
    return arrivals;
  }

  // Where a played card lands, from the actor's point of view, expressed in
  // the board's own zone names (which are relative to the human player).
  function targetZone(target, actorPlayer, humanPlayer) {
    if (typeof target !== "string" || !target) return null;
    const actorIsHuman = actorPlayer === humanPlayer;
    const actorsOwnSide = target.startsWith("own_");
    const humanOwnsIt = actorsOwnSide === actorIsHuman;
    const slot = target.endsWith("_rearguard") ? "rearguard" : "active";
    return `${humanOwnsIt ? "own" : "opponent"}_${slot}`;
  }

  /* Plays worth a ceremony, in the order the engine resolved them. Each entry
     carries the card (with its instance when one could be paired), where it
     ended up, and which board zone should take the hit. */
  function playEvents(prev, next) {
    if (!prev || !next) return [];
    const human = next.humanPlayer;
    const pool = [
      ...newDiscards(prev, next).map(card => ({ card, destination: "discard", used: false })),
      ...newEquipment(prev, next).map(({ card }) => ({ card, destination: "equipment", used: false })),
    ];
    const events = [];
    for (const entry of newLogEntries(prev, next)) {
      const parsed = ENGINE_CALL.exec(entry.engineText || "");
      if (!parsed) continue;
      const [, verb, key, target] = parsed;
      if (!CEREMONY_VERBS.has(verb)) continue;
      const slot = pool.find(candidate => !candidate.used && candidate.card.key === key);
      if (slot) slot.used = true;
      // A card answering a hit protects the side that played it; everything
      // else lands on the target the engine recorded.
      const zoneTarget = verb === "avoid" ? "own_active" : target;
      events.push({
        verb,
        key,
        player: entry.player,
        isHuman: entry.player === human,
        card: slot ? slot.card : { key, kind: null, instance: null, name: key },
        instance: slot ? slot.card.instance : null,
        destination: slot ? slot.destination : null,
        zone: targetZone(zoneTarget, entry.player, human),
        motif: motifFor(slot ? slot.card : { key }, verb),
      });
    }
    return events;
  }

  /* ------------------------------------------------------------------
     Reading the seats out of the state diff.

     Characters never move between seats, so a character the previous state
     had and this one does not has fallen, and one this state has that the
     previous did not has just taken a seat — either a replacement out of the
     reserve, or the opponent's opening pair turning face up once both sides
     have committed.
     ------------------------------------------------------------------ */

  function seatedCharacters(state) {
    const seated = new Map();
    (state?.players || []).forEach((player, index) => {
      const own = index === state.humanPlayer;
      for (const position of player.positions || []) {
        if (!position.character) continue;
        seated.set(position.character.id, {
          id: position.character.id,
          key: position.character.key,
          name: position.character.name,
          life: position.life,
          side: player.side,
          own,
          // What is bolted to this character right now. The 3D stage keeps
          // these standing on the field for as long as the board says they
          // are in play, so it needs the row rather than only the arrivals.
          equipment: (position.equipment || []).map(card => card.key),
          zone: `${own ? "own" : "opponent"}_${position.role === "active" ? "active" : "rearguard"}`,
        });
      }
    });
    return seated;
  }

  function characterEvents(prev, next) {
    if (!prev || !next) return { fallen: [], arrivals: [] };
    const before = seatedCharacters(prev);
    const after = seatedCharacters(next);
    const fallen = [];
    for (const [id, who] of before) {
      // A character already lying at zero life took its topple and its skull
      // when it went down; losing the seat is only the table being cleared.
      if (!after.has(id)) fallen.push({ ...who, swept: (who.life ?? 0) <= 0 });
    }
    const arrivals = [];
    for (const [id, who] of after) {
      if (!before.has(id)) arrivals.push({ ...who, replacement: !!prev.setupComplete });
    }
    return { fallen, arrivals };
  }

  // [third person, second person] — the plaque addresses you directly.
  const VERB_LABELS = {
    play: ["plays", "play"],
    play_as_colt: ["fire as a Colt", "fire as a Colt"],
    avoid: ["blocks with", "block with"],
    use_resource: ["uses", "use"],
  };

  function playCaption(event, actorName) {
    const forms = VERB_LABELS[event.verb] || VERB_LABELS.play;
    return `${actorName} ${forms[event.isHuman ? 1 : 0]}`;
  }

  /* ------------------------------------------------------------------
     Timing.

     One shared schedule drives both the visuals and the sound cues, so a
     gunshot cracks exactly when the card lands on its target. Reduced motion
     collapses the ceremony to nothing but keeps the cues staggered, so the
     audio still narrates the opponent's turn card by card.
     ------------------------------------------------------------------ */

  const TIMING = { enter: 420, exit: 500, gap: 90, flatGap: 150 };

  function planPlays(prev, next, options = {}) {
    const events = playEvents(prev, next);
    const flat = !!options.reducedMotion;
    // A long opponent turn would otherwise hold the table hostage.
    const squeeze = events.length > 2 ? 0.68 : 1;
    let at = options.startAt || 0;
    return events.map(event => {
      const enter = flat ? 0 : TIMING.enter;
      const exit = flat ? 0 : TIMING.exit;
      // You already know what you just played; the opponent's cards get the
      // full pause so the play can be read.
      const hold = flat ? 0 : Math.round(event.motif.hold * (event.isHuman ? 0.42 : 1) * squeeze);
      const plan = {
        ...event,
        enterAt: at,
        impactAt: at + enter,
        holdMs: hold,
        exitAt: at + enter + hold,
        exitMs: exit,
        endsAt: at + enter + hold + exit,
      };
      at = plan.endsAt + (flat ? TIMING.flatGap : TIMING.gap);
      return plan;
    });
  }

  return {
    TIMING,
    planPlays,
    MOTIFS,
    MOTIF_BY_KEY,
    motifNameFor,
    motifFor,
    newLogEntries,
    newDiscards,
    newEquipment,
    seatedCharacters,
    characterEvents,
    targetZone,
    playEvents,
    playCaption,
  };
});
