(function (root, factory) {
  "use strict";

  const api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    root.BangHistory = api;
  }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  /* ------------------------------------------------------------------
     The trail: folding the action log into moments.

     The server's log is a flat list of resolved actions ("play(colt,
     opponent_active)", "avoid(missed)", …). The story of a duel is not
     flat: a Missed! only means anything as the answer to the BANG! that
     provoked it, and a General Store draft only as the fallout of the
     Store being played. This module folds the flat log into that shape —
     a sequence of *moments*, each one a main play carrying every action
     it caused — so the history view can show a card with its answers
     tucked behind it.

     The fold leans on how the engine logs (src/action_space.cpp): every
     entry between one committed play and the next is that play still
     resolving — responses to its hits (avoid / take_hit), picks from the
     choice it opened (select / choose_back / discard), and the deaths it
     caused. So attribution is positional, not a re-derivation of rules:
     the frontend keeps trusting the engine's own ordering.
     ------------------------------------------------------------------ */

  // Humanised card / character keys; the punctuation a key cannot carry.
  const NAME_OVERRIDES = { missed: "Missed!", indians: "Indians!" };

  function cardName(key) {
    if (!key) return "";
    return NAME_OVERRIDES[key] || String(key).split("_")
      .map(word => word.charAt(0).toUpperCase() + word.slice(1)).join(" ");
  }

  const ENGINE_CALL = /^([a-z_]+)(?:\((.*)\))?$/;

  function parseEngine(engineText) {
    const match = ENGINE_CALL.exec(engineText || "");
    if (!match) return null;
    return { verb: match[1], args: match[2] ? match[2].split(",") : [] };
  }

  // "Soundance Kid: Play Colt" → "Soundance Kid" — the character acting.
  function actorOf(text) {
    const split = String(text || "").indexOf(": ");
    return split > 0 ? String(text).slice(0, split) : "";
  }

  // Verbs that commit a card or an ability — each starts a moment of its own.
  const MAIN_VERBS = new Set(["play", "play_as_colt", "use_ability", "reload"]);

  /* Fold the log into moments. Each moment is one of:
       play      a committed card, with everything it caused in `responses`
       ability   a character ability used, with its costs and picks
       reload    a card recycled for a fresh draw
       swap      a voluntary active/rearguard swap
       turn_end  the turn closing — hand-limit discards and the deaths of
                 the next turn's start-of-turn reveals gather here
       setup     a face-down setup choice
       system    a dealer's announcement ("New duel …")
       prior     responses whose main play fell off the trimmed log
     Responses are one of:
       card      a card committed in answer (avoid, duel exchange, draft
                 pick, forced discard, ability cost …), with `role`
       back      a hidden hand card taken by its card back (Stealing)
       hit       a hit taken (take_hit, or conceding a Duel)
       death     a character falling                                     */
  function buildHistory(log) {
    const moments = [];
    let open = null;   // the moment still collecting what it caused
    let prior = null;  // orphaned responses from a trimmed log head

    const attach = response => {
      if (open) { open.responses.push(response); return; }
      if (!prior) {
        prior = { type: "prior", responses: [] };
        moments.unshift(prior);
      }
      prior.responses.push(response);
    };

    for (const entry of log || []) {
      const parsed = parseEngine(entry.engineText);
      const base = {
        player: entry.player ?? null,
        side: entry.side || "",
        actor: actorOf(entry.text),
        text: entry.text || "",
      };
      if (!parsed) {
        moments.push({ type: "system", ...base });
        open = null;
        continue;
      }
      const { verb, args } = parsed;
      if (MAIN_VERBS.has(verb)) {
        open = {
          type: verb === "use_ability" ? "ability" :
                verb === "reload" ? "reload" : "play",
          ...base,
          key: args[0] || "",
          name: cardName(args[0]),
          target: args[1] || null,
          asColt: verb === "play_as_colt",
          responses: [],
        };
        moments.push(open);
        continue;
      }
      switch (verb) {
        case "swap":
          moments.push({ type: "swap", ...base });
          open = null;
          break;
        case "end_play":
          // Stays open: the hand-limit discards that follow, and any deaths
          // from the next turn's start-of-turn reveals, belong to the break.
          open = { type: "turn_end", ...base, responses: [] };
          moments.push(open);
          break;
        case "choose_active":
          moments.push({ type: "setup", ...base });
          open = null;
          break;
        case "avoid":
          attach({ kind: "card", role: "avoid", key: args[0] || "",
                   name: cardName(args[0]), ...base });
          break;
        case "take_hit":
          attach({ kind: "hit", ...base });
          break;
        case "death":
          attach({ kind: "death", key: args[0] || "",
                   name: cardName(args[0]), ...base });
          break;
        case "discard":
          // A discard is whatever its moment makes it: a Duel's hit card, a
          // Wagon's give-back, an ability's cost, the hand limit at turn end.
          attach({ kind: "card",
                   role: open?.type === "turn_end" ? "hand_limit" : "discard",
                   key: args[0] || "", name: cardName(args[0]), ...base });
          break;
        case "select":
          attach({ kind: "card", role: "pick", key: args[0] || "",
                   name: cardName(args[0]), ...base });
          break;
        case "choose_back":
          attach({ kind: "back", origin: args[0] || "", ...base });
          break;
        case "skip_optional":
          // Inside a Duel, declining is conceding: the active loses 1 life.
          if (open?.type === "play" && open.key === "duel") {
            attach({ kind: "hit", concede: true, ...base });
          }
          break;
        default:
          // accept_optional, choose_reveal, choose_swap …: decisions whose
          // visible outcome (a draw, a swap, a reveal) has no card of its
          // own to show. They stay in the text log; the trail skips them.
          break;
      }
    }
    return moments;
  }

  return { buildHistory, parseEngine, cardName, actorOf };
});
